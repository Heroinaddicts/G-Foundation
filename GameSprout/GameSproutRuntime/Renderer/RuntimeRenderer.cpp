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
    _Error.clear();_Statistics={};_ShadowRecorded=false;
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
        if(view.DrawScene && !Extract(view,_SceneObjects)) return false;
        auto& lights=_SceneLights;
        if(view.DrawScene&&!ExtractLights(view.Scene,lights))return false;
        RenderEnvironment::Uniform environment{};
        if(view.DrawScene&&!RenderEnvironment::Prepare(view.Scene->GetEnvironmentLighting(),environment))
            Diagnostic("Scene environment radiance is invalid or exceeds finite shader precision; diffuse environment disabled for this frame");
        if(view.DrawScene)PrepareDraws(view,viewProjection);
        const std::vector<PreparedDraw> clearDraws;
        const auto& prepared=view.DrawScene?_PreparedDraws:clearDraws;
        if(view.DrawScene&&!RenderShadows(*command.Command,view,float(width)/height))return false;
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
        // A recorded atlas is reusable only after the whole command is submitted.
        // A later color-pass error/cancel must not cache an unexecuted depth pass.
        if(_ShadowRecorded)_ShadowValid=true;
        if(view.DrawScene){_RenderedCamera={view.Camera,viewProjection,width,height,view.Scene->GetInstanceID()};_HasRenderedCamera=true;}
        return true;
    } catch(const std::exception& exception) {return Fail(std::string("Runtime Renderer: ")+exception.what());}
    catch(...) {return Fail("Unexpected Runtime Renderer failure");}
}
bool Renderer::GetRenderedCamera(RenderedCamera& camera) const {
    camera=_HasRenderedCamera?_RenderedCamera:RenderedCamera{};return _HasRenderedCamera;
}
}
