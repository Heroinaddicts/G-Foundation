#include "RuntimeRenderer.h"
#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Scene.h"
#include "Components/MeshComponent.h"
#include "RuntimeMath.h"
#include <exception>
#include <algorithm>

namespace {
struct CommandScope {
    GRHI::IDevice& Device;
    GRHI::ICommandBuffer* Command=nullptr;
    GRHI::IRenderPass* Pass=nullptr;
    bool AcquiredFrame=false;
    ~CommandScope() {
        if(!Command) return;
        try {
            if(Pass) Command->EndRenderPass(Pass);
            if(AcquiredFrame) Device.Submit(Command);else Device.Cancel(Command);
        } catch(...) {}
    }
};
struct RenderingScope {
    bool& Value;
    explicit RenderingScope(bool& value):Value(value) {Value=true;}
    ~RenderingScope() {Value=false;}
};
struct DebugBufferScope {
    std::vector<GRHI::IBuffer*> Buffers;
    ~DebugBufferScope() {for(auto* buffer:Buffers) delete buffer;}
};
}
namespace GameSproutRuntime {
bool Renderer::Render(const RenderView& view) {
    _HasRenderedCamera=false;_RenderedCamera={};
    if(_Rendering || _Runtime.IsExecuting() || _Runtime.IsShuttingDown()) return Fail("Runtime Renderer cannot run inside a Runtime callback or another Render");
    _Error.clear();_Statistics={};
    if(!_Surface&&!_ColorTarget){_Statistics={};return true;}
    if(_Surface&&_ColorTarget)return Fail("Viewport has conflicting render targets");
    if(view.DrawScene) {
        auto* scene=_Runtime.Find(view.Scene);
        if(!_Runtime.IsUsable(scene) || scene->SceneOwner!=view.Scene) return Fail("RenderView scene is not a live scene owned by this Runtime");
        if(!RuntimeMath::IsFinite(view.Camera.View) || !RuntimeMath::IsFinite(view.Camera.Position))
            return Fail("RenderView camera is nonfinite");
    }
    if(!RuntimeMath::IsFinite(view.ClearColor)) return Fail("RenderView clear color is nonfinite");
    RenderingScope rendering(_Rendering);
    RuntimeOperation operation(_Runtime);
    try {
        ++_Frame;
        if(view.DrawScene) {
            SyncRevision();
            if(!Initialize()) return false;
        }
        std::vector<DebugBatch> debugBatches;
        if(view.DrawScene && !PrepareDebug(view.DebugDraw,debugBatches)) return false;
        std::vector<DrawObject> objects;
        if(view.DrawScene && !Extract(view,objects)) return false;
        struct Prepared {const DrawObject* Object;MeshGPU* Mesh;MeshSection Section;MaterialRenderData Material;TextureGPU* Texture=nullptr;};
        std::vector<Prepared> prepared;
        for(auto& object:objects) {
            auto* mesh=Mesh(object.Mesh);
            if(mesh && object.Skinned) {
                auto* node=_Runtime.QueryGameObject(object.ObjectID);
                if(!node || !SkinMatrices(*node,*mesh,object.SkinMatrices)) {++_Statistics.SkippedObjects;continue;}
            }
            // 更新已加载资源的派生 bounds；不修改节点属性、主相机、Undo 或源场景快照。
            if(auto* node=_Runtime.QueryGameObject(object.ObjectID)) {
                if(auto* component=dynamic_cast<MeshComponent*>(node->GetComponent<IMeshComponent>());
                   component && component->GetMesh()==object.Mesh) {
                    component->_Bounds=mesh?mesh->Data.LocalBounds:Bounds{};
                    if(mesh && object.Skinned) {
                        std::vector<Vector3> positions;
                        if(!PosedPositions(*node,*mesh,positions)) {++_Statistics.SkippedObjects;continue;}
                        if(!positions.empty()) {
                            Vector3 minimum=positions.front(),maximum=minimum;
                            for(const auto& point:positions) {
                                minimum={std::min(minimum.X,point.X),std::min(minimum.Y,point.Y),std::min(minimum.Z,point.Z)};
                                maximum={std::max(maximum.X,point.X),std::max(maximum.Y,point.Y),std::max(maximum.Z,point.Z)};
                            }
                            component->_Bounds={minimum*0.5f+maximum*0.5f,maximum*0.5f-minimum*0.5f};
                        }
                    }
                }
            }
            if(!mesh) {++_Statistics.SkippedObjects;continue;}
            bool drawn=false,skipped=false;
            for(const auto& section:mesh->Data.Sections) {
                if(!section.IndexCount) continue;
                MaterialRenderData material;
                if(section.MaterialSlot<object.Materials.size()) {
                    const auto& reference=object.Materials[section.MaterialSlot];
                    if(!reference.Guid.empty() || reference.LocalID) {
                        auto* cached=MaterialData(reference);
                        if(!cached) {skipped=true;continue;}
                        material=cached->Data;
                    }
                }
                TextureGPU* texture=nullptr;
                if(mesh->Data.UVOffset>=0 && !material.BaseColorTexture.Guid.empty()) {
                    texture=Texture(material.BaseColorTexture);
                    if(!texture) {skipped=true;continue;}
                }
                prepared.push_back({&object,mesh,section,std::move(material),texture});drawn=true;
            }
            if(drawn) ++_Statistics.VisibleObjects;
            if(skipped || !drawn) ++_Statistics.SkippedObjects;
        }
        // 缓冲在Command提交/取消后释放；异常退出也保持这一顺序。
        DebugBufferScope debugBuffers;
        CommandScope command{_Device};command.Command=_Device.AcquireCommandBuffer();
        if(!command.Command) return Fail(DeviceError("Acquire Runtime render command"));
        GRHI::ColorTargetView target;
        GRHI::TextureFormat format=GRHI::TextureFormat::Unknown;
        std::uint32_t width=0,height=0;
        if(_Surface) {
            GRHI::SurfaceFrame frame;
            const auto result=_Surface->AcquireFrame(command.Command,frame);
            if(result==GRHI::AcquireFrameResult::Unavailable) {
                const bool cancelled=_Device.Cancel(command.Command);
                if(cancelled) command.Command=nullptr;
                _Statistics={};
                return cancelled?true:Fail(DeviceError("Cancel unavailable Runtime view"));
            }
            if(result!=GRHI::AcquireFrameResult::Ready) return Fail(DeviceError("Acquire Runtime surface frame"));
            command.AcquiredFrame=true;target=frame.ColorTarget;width=frame.Width;height=frame.Height;format=frame.Format;
        } else {
            target=_ColorTarget->GetColorTargetView();width=_ColorTarget->GetWidth();height=_ColorTarget->GetHeight();format=_ColorTarget->GetFormat();
        }
        if(!width || !height || !target.Token) return Fail("Runtime render target has no valid color view or pixel extent");
        Mat4 viewProjection;
        GRHI::ITexture* depth=nullptr;
        if(view.DrawScene) {
            Mat4 projection;
            const float aspect=float(width)/float(height);
            bool valid=false;
            if(view.Camera.Projection.Projection==CameraProjection::Perspective) valid=TryMakePerspective(view.Camera.Projection.VerticalFovRadians,aspect,
                view.Camera.Projection.NearPlane,view.Camera.Projection.FarPlane,projection);
            else if(view.Camera.Projection.Projection==CameraProjection::Orthographic) valid=TryMakeOrthographic(view.Camera.Projection.OrthographicHeight,aspect,
                view.Camera.Projection.NearPlane,view.Camera.Projection.FarPlane,projection);
            viewProjection=projection*view.Camera.View;
            if(!valid || !RuntimeMath::IsFinite(viewProjection)) return Fail("Invalid explicit RenderView camera projection");
            depth=DepthTarget(width,height);if(!depth) return false;
        }
        GRHI::RenderPassDesc pass;
        pass.ColorTarget=target;pass.ClearColor={view.ClearColor.R,view.ClearColor.G,view.ClearColor.B,view.ClearColor.A};
        if(depth){pass.DepthTarget=depth->GetDepthTargetView();pass.DepthLoad=GRHI::LoadOp::Clear;pass.ClearDepth=1.0f;}
        command.Pass=command.Command->BeginRenderPass(pass);
        if(!command.Pass) return Fail(DeviceError("Begin Runtime depth/color pass"));
        _Statistics.Width=width;_Statistics.Height=height;
        if(view.DrawScene && view.DrawGrid && !DrawGrid(*command.Pass,viewProjection,format)) return false;
        for(const auto& draw:prepared) {
            const auto& data=draw.Mesh->Data;
            PipelineKey key;key.Format=format;key.Stride=data.VertexStride;key.Position=data.PositionOffset;key.Normal=data.NormalOffset;
            key.Color=data.ColorOffset;key.UV=data.UVOffset;key.DoubleSided=draw.Material.DoubleSided;key.Negative=draw.Object->Negative;
            if(draw.Object->Skinned) {key.BoneIndices=data.BoneIndicesOffset;key.BoneWeights=data.BoneWeightsOffset;}
            auto* pipeline=Pipeline(key);if(!pipeline) return false;
            DrawUniform uniform;uniform.Model=draw.Object->Model;uniform.ViewProjection=viewProjection;
            uniform.Normal=draw.Object->Normal;uniform.BaseColor=draw.Material.BaseColor;
            uniform.TextureST=draw.Material.BaseColorTextureST;
            command.Pass->SetPipeline(pipeline);
            command.Pass->SetVertexBuffer(0,draw.Mesh->Buffer,0);
            command.Pass->SetIndexBuffer(draw.Mesh->Buffer,GRHI::IndexFormat::UInt32,static_cast<std::size_t>(data.IndexOffset));
            command.Pass->SetVertexUniform(0,&uniform,sizeof(uniform));
            if(draw.Object->Skinned) {
                SkinUniform skin;
                std::copy(draw.Object->SkinMatrices.begin(),draw.Object->SkinMatrices.end(),skin.Bones);
                command.Pass->SetVertexUniform(1,&skin,sizeof(skin));
            }
            command.Pass->SetFragmentTextureSampler(0,draw.Texture?draw.Texture->Texture:_White,draw.Texture?draw.Texture->Sampler:_DefaultSampler);
            command.Pass->DrawIndexed(draw.Section.IndexCount,1,draw.Section.FirstIndex,0,0);
            ++_Statistics.DrawCalls;_Statistics.Triangles+=draw.Section.IndexCount/3;
        }
        if(view.DrawScene && !DrawDebug(*command.Pass,viewProjection,format,debugBatches,debugBuffers.Buffers)) return false;
        command.Command->EndRenderPass(command.Pass);command.Pass=nullptr;
        auto* submitted=command.Command;command.Command=nullptr;
        if(!_Device.Submit(submitted)) return Fail(DeviceError("Submit Runtime view"));
        if(view.DrawScene){_RenderedCamera={view.Camera,viewProjection,width,height,view.Scene->GetInstanceID()};_HasRenderedCamera=true;}
        return true;
    } catch(const std::exception& exception) {return Fail(std::string("Runtime Renderer: ")+exception.what());}
    catch(...) {return Fail("Unexpected Runtime Renderer failure");}
}
bool Renderer::GetRenderedCamera(RenderedCamera& camera) const {
    camera=_HasRenderedCamera?_RenderedCamera:RenderedCamera{};return _HasRenderedCamera;
}
}
