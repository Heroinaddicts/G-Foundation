#include "RuntimeRenderer.h"
#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Scene.h"
#include "Components/MeshComponent.h"
#include "RuntimeMath.h"
#include "RuntimeFrustum.h"
#include "RuntimeEnvironment.h"
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
    if(_Rendering || _Runtime.IsExecuting() || _Runtime.IsShuttingDown() || _Runtime.IsConstructing()) return Fail("Runtime Renderer cannot run inside a Runtime callback, construction or another Render");
    _Error.clear();_Statistics={};
    _PreparedDraws.clear();
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
        PruneInstanceBuffers();
        if(view.DrawScene) {
            SyncRevision();
            if(!Initialize()) return false;
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
        std::vector<DebugBatch> debugBatches;
        if(view.DrawScene && !PrepareDebug(view.DebugDraw,debugBatches)) return false;
        std::vector<DrawObject> emptyObjects;
        auto& objects=view.DrawScene?_SceneObjects:emptyObjects;
        if(view.DrawScene && !Extract(view,objects)) return false;
        const RenderFrustum::Frustum frustum(viewProjection);
        auto& lights=_SceneLights;
        if(view.DrawScene&&!ExtractLights(view.Scene,lights))return false;
        RenderEnvironment::Uniform environment{};
        if(view.DrawScene&&!RenderEnvironment::Prepare(view.Scene->GetEnvironmentLighting(),environment))
            Diagnostic("Scene environment radiance is invalid or exceeds finite shader precision; diffuse environment disabled for this frame");
        const MaterialRenderData defaultMaterial;
        static const ResourceReference defaultMaterialReference;
        auto& prepared=_PreparedDraws;
        for(auto& object:objects) {
            if(!object.Geometry)object.Geometry=Mesh(object.Mesh);
            auto* mesh=object.Geometry;
            if(mesh && object.Skinned) {
                auto* node=_Runtime.QueryGameObject(object.ObjectID);
                if(!node || !SkinMatrices(*node,*mesh,object.SkinMatrices)) {++_Statistics.SkippedObjects;continue;}
            }
            // 更新已加载资源的派生 bounds；不修改节点属性、主相机、Undo 或源场景快照。
            if(object.Skinned||!object.BoundsInitialized)if(auto* node=_Runtime.QueryGameObject(object.ObjectID)) {
                if(auto* component=dynamic_cast<MeshComponent*>(node->GetComponent<IMeshComponent>());
                   component && component->GetMesh()==object.Mesh) {
                    component->_Bounds=mesh?mesh->Data.LocalBounds:Bounds{};
                    object.BoundsInitialized=true;
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
            bool outside=false;
            if(!object.Skinned) {
                if(!mesh->CullBoundsKnown&&!mesh->Positions.empty()) {
                    mesh->CullMinimum=mesh->CullMaximum=mesh->Positions.front();
                    for(const auto& point:mesh->Positions) {
                        mesh->CullMinimum={std::min(mesh->CullMinimum.X,point.X),std::min(mesh->CullMinimum.Y,point.Y),std::min(mesh->CullMinimum.Z,point.Z)};
                        mesh->CullMaximum={std::max(mesh->CullMaximum.X,point.X),std::max(mesh->CullMaximum.Y,point.Y),std::max(mesh->CullMaximum.Z,point.Z)};
                    }
                    mesh->CullBoundsKnown=true;
                }
                if(mesh->CullBoundsKnown)outside=!frustum.Intersects(mesh->CullMinimum,mesh->CullMaximum,object.Model);
            }
            bool drawn=false,skipped=false;
            for(const auto& section:mesh->Data.Sections) {
                if(!section.IndexCount) continue;
                const MaterialRenderData* material=&defaultMaterial;
                Material* cachedMaterial=nullptr;
                const ResourceReference* materialReference=&defaultMaterialReference;
                if(section.MaterialSlot<object.Materials.size()) {
                    const auto& reference=object.Materials[section.MaterialSlot];
                    if(!reference.Guid.empty() || reference.LocalID) {
                        auto*& cached=object.MaterialCache[section.MaterialSlot];
                        if(!cached)cached=MaterialData(reference);
                        if(!cached) {skipped=true;continue;}
                        cachedMaterial=cached;material=&cached->Data;materialReference=&reference;
                    }
                }
                // User vertex shaders have no displacement-bounds contract yet;
                // never reject them using undeformed mesh bounds. Skinned meshes
                // are likewise retained regardless of their bind-pose position.
                if(outside&&material->Shader.Guid.empty()){skipped=true;continue;}
                TextureGPU* texture=nullptr;TextureGPU* normalTexture=nullptr;
                ShaderGPU* shader=nullptr;bool transparent=false;
                if(!material->Shader.Guid.empty()) {
                    if(object.Skinned){Diagnostic("User Shader currently supports static meshes; skinned Shader variants are not implemented");skipped=true;continue;}
                    shader=Shader(material->Shader);
                    if(!shader||!PrepareShaderMaterial(*cachedMaterial,*shader)||!EnsureShaderVertices(*mesh,object.Mesh)){skipped=true;continue;}
                    transparent=shader->Data.Passes[0].Blend!=ShaderBlendMode::Opaque;
                }else if(mesh->Data.UVOffset>=0 && !material->BaseColorTexture.Guid.empty()) {
                    texture=Texture(material->BaseColorTexture);
                    if(!texture) {skipped=true;continue;}
                }
                if(material->ShadingModel==MaterialShadingModel::Lit&&!material->NormalTexture.Guid.empty()) {
                    if(mesh->Data.UVOffset<0){Diagnostic("Lit normal map requires mesh UV coordinates");skipped=true;continue;}
                    normalTexture=Texture(material->NormalTexture);
                    if(!normalTexture){skipped=true;continue;}
                    if(normalTexture->SRGB){Diagnostic("Lit normal map must be imported in Linear color space");skipped=true;continue;}
                }
                const Vector3 difference{object.Model(0,3)-view.Camera.Position.X,object.Model(1,3)-view.Camera.Position.Y,object.Model(2,3)-view.Camera.Position.Z};
                const float distance=difference.X*difference.X+difference.Y*difference.Y+difference.Z*difference.Z;
                prepared.push_back({&object,mesh,section,material,materialReference,texture,normalTexture,cachedMaterial,shader,transparent,distance,
                    static_cast<std::uint64_t>(&section-mesh->Data.Sections.data())});drawn=true;
            }
            if(drawn) ++_Statistics.VisibleObjects;
            if(skipped || !drawn) ++_Statistics.SkippedObjects;
        }
        SortPrepared(prepared);
        GRHI::RenderPassDesc pass;
        pass.ColorTarget=target;pass.ClearColor={view.ClearColor.R,view.ClearColor.G,view.ClearColor.B,view.ClearColor.A};
        if(depth){pass.DepthTarget=depth->GetDepthTargetView();pass.DepthLoad=GRHI::LoadOp::Clear;pass.ClearDepth=1.0f;}
        command.Pass=command.Command->BeginRenderPass(pass);
        if(!command.Pass) return Fail(DeviceError("Begin Runtime depth/color pass"));
        _Statistics.Width=width;_Statistics.Height=height;
        if(view.DrawScene && view.DrawGrid && !DrawGrid(*command.Pass,viewProjection,format)) return false;
        if(!DrawPrepared(*command.Pass,prepared,view,viewProjection,format,lights,&environment,sizeof(environment)))return false;
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
