#include "SceneViewport.h"
#include "SceneViewportEditing.h"
#include "SceneToolRouter.h"
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QWindow>
#include <QPaintEngine>
#include <cmath>

bool SceneViewport::IsHeadless(){const QString name=QGuiApplication::platformName();return name=="offscreen"||name.startsWith("minimal");}
SceneViewport::SceneViewport(QWidget* parent):QWidget(parent) {
    _Editing=new SceneViewportEditing(*this);
    _Tools=new SceneToolRouter(*this);
    _Camera.Changed=[this]{RequestRender();};
    _WheelNavigationSettle.setSingleShot(true);
    _WheelNavigationSettle.setTimerType(Qt::PreciseTimer);
    _WheelNavigationSettle.setInterval(180);
    connect(&_WheelNavigationSettle,&QTimer::timeout,this,[this] {
        if(_Drag==DragMode::None)RequestRender();
    });
    if(!IsHeadless()) {
        setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_DontCreateNativeAncestors);
        setAttribute(Qt::WA_PaintOnScreen);
        setAttribute(Qt::WA_NoSystemBackground);
    }
}
SceneViewport::~SceneViewport(){Shutdown();delete _Tools;_Tools=nullptr;delete _Editing;_Editing=nullptr;}
void SceneViewport::BindEditingServices(ISceneManager* scenes,ISelectionManager* selection){_Editing->Bind(scenes,selection);_Tools->Bind(scenes,selection);}
QPaintEngine* SceneViewport::paintEngine() const{return IsHeadless()?QWidget::paintEngine():nullptr;}
void SceneViewport::Initialize(GameSproutRuntime::IGameSproutRuntime* runtime,GameSproutRuntime::IRuntimeResourceProvider* resources) {
    Shutdown();_Runtime=runtime;_Initialized=true;_Resources=resources;
    if(_Runtime)_RuntimeSubscribed=_Runtime->RegisterListener(this);
    RequestRender();
    if(IsHeadless())SetStatus("离屏测试环境：保留 Scene 视图与独立相机，不创建原生 GPU 窗口。",true);
    else if(!NativePlatformSupported())SetStatus("当前 Qt 平台尚无兼容的 Runtime 原生窗口句柄。",true);
}
bool SceneViewport::NativePlatformSupported() const {
    const QString name=QGuiApplication::platformName();
#if defined(__APPLE__)
    return name=="cocoa";
#elif defined(_WIN32)
    return name=="windows";
#else
    return name=="xcb";
#endif
}
void SceneViewport::SetStatus(const std::string& message,bool error) {
    _Error=error?message:"";
    if(_Status==message)return;
    _Status=message;const auto callback=StatusChanged;if(callback)callback(message);
}
void SceneViewport::DestroyGpu() {
    // Renderer 缓存先释放；Surface 必须在 NSView/HWND/X11 Window 仍有效时解除绑定。
    // 先摘空成员，SDL 释放外来窗口时若同步触发原生 Qt 事件，重复清理仍然安全。
    auto* renderer=_Renderer;_Renderer=nullptr;
    _AnimatedMaterials=false;
    _PresentedCamera={};
    _NativeID=0;_Attempted=false;
    _ViewportChanged=true;
    RequestRender();
    emit FrameStreamReset();
    delete renderer;
}
void SceneViewport::Shutdown() {
    if(_RuntimeSubscribed&&_Runtime)_Runtime->UnregisterListener(this);
    _RuntimeSubscribed=false;_SceneID=0;
    _Tools->Shutdown();
    _Editing->Unbind();
    ClearInput();DestroyGpu();
    if(_NativeWindow){_NativeWindow->removeEventFilter(this);QObject::disconnect(_NativeWindow,nullptr,this,nullptr);_NativeWindow=nullptr;}
    if(_HostWindow){_HostWindow->removeEventFilter(this);QObject::disconnect(_HostWindow,nullptr,this,nullptr);_HostWindow=nullptr;}
    _Resources=nullptr;
    _Runtime=nullptr;_Initialized=false;
}
void SceneViewport::RetryRendering(){ClearInput();DestroyGpu();}
void SceneViewport::SetShadowReuseWhileNavigatingEnabled(bool enabled) {
    if(_ReuseShadowsWhileNavigating==enabled)return;
    _ReuseShadowsWhileNavigating=enabled;
    if(!enabled)_WheelNavigationSettle.stop();
    RequestRender();
}
void SceneViewport::OnSceneVisualChanged(std::uint64_t sceneID) {if(sceneID==_SceneID)RequestRender();}
bool SceneViewport::GetRenderedCamera(GameSproutRuntime::RenderedCamera& camera) const {
    camera=_ViewportChanged ? GameSproutRuntime::RenderedCamera{} : _PresentedCamera;
    return camera.SceneID!=0;
}
void SceneViewport::ObserveNativeWindow() {
    QWindow* native=windowHandle();
    if(native!=_NativeWindow) {
        if(_NativeWindow){_NativeWindow->removeEventFilter(this);QObject::disconnect(_NativeWindow,nullptr,this,nullptr);}
        _NativeWindow=native;
        if(native) {
            native->installEventFilter(this);
            QObject::connect(native,&QObject::destroyed,this,[this,native]{if(_NativeWindow==native)_NativeWindow=nullptr;});
        }
    }
    QWindow* host=window()?window()->windowHandle():nullptr;
    if(host==native)host=nullptr;
    if(host!=_HostWindow) {
        if(_HostWindow){_HostWindow->removeEventFilter(this);QObject::disconnect(_HostWindow,nullptr,this,nullptr);}
        _HostWindow=host;
        if(host) {
            host->installEventFilter(this);
            QObject::connect(host,&QObject::destroyed,this,[this,host]{if(_HostWindow==host)_HostWindow=nullptr;});
        }
    }
}
bool SceneViewport::event(QEvent* event) {
    if(event->type()==QEvent::PlatformSurface && static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType()==QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed){ClearInput();DestroyGpu();}
    else if(event->type()==QEvent::WinIdChange){ClearInput();DestroyGpu();ObserveNativeWindow();}
    else if(event->type()==QEvent::Resize||event->type()==QEvent::ScreenChangeInternal
#if QT_VERSION >= QT_VERSION_CHECK(6,6,0)
            ||event->type()==QEvent::DevicePixelRatioChange
#endif
    ){ClearInput();_ViewportChanged=true;RequestRender();}
    else if(event->type()==QEvent::Leave&&!_Editing->IsDragging()){
        _Tools->Move({-100,-100},false,false,false);_Editing->Move({-100,-100},false);
    }
    else if(event->type()==QEvent::WindowDeactivate||event->type()==QEvent::UngrabMouse)ClearInput();
    if(event->type()==QEvent::Show||event->type()==QEvent::Paint||event->type()==QEvent::Expose||
       event->type()==QEvent::WindowActivate||event->type()==QEvent::ScreenChangeInternal)RequestRender();
    return QWidget::event(event);
}
bool SceneViewport::eventFilter(QObject* watched,QEvent* event) {
    if((watched==_NativeWindow||watched==_HostWindow) && event->type()==QEvent::PlatformSurface) {
        const auto kind=static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType();
        if(kind==QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed){ClearInput();DestroyGpu();}
        else if(kind==QPlatformSurfaceEvent::SurfaceCreated){_Attempted=false;RequestRender();}
    }
    if((watched==_NativeWindow||watched==_HostWindow)&&
       (event->type()==QEvent::Expose||event->type()==QEvent::WindowActivate||event->type()==QEvent::ScreenChangeInternal))RequestRender();
    return QWidget::eventFilter(watched,event);
}
bool SceneViewport::CanRender() const {
    if(!_Initialized || IsHeadless() || !NativePlatformSupported() || !isVisible() || width()<=0 || height()<=0)return false;
    const QWidget* host=window();
    return host && !host->isMinimized() && host->windowHandle() && host->windowHandle()->isExposed();
}
bool SceneViewport::EnsureRendering() {
    if(_Renderer)return true;
    if(_Attempted)return false;
    _Attempted=true;
    if(!_Runtime||!_Resources){SetStatus("Scene 渲染依赖的 Runtime 或资源服务尚未就绪。",true);return false;}
    // 只在真实、已显示的原生平台调用 winId，绝不把 offscreen/minimal 的伪 ID 交给 Cocoa。
    const WId id=winId();ObserveNativeWindow();
    _Attempted=true;
    if(!id){SetStatus("Scene 原生渲染窗口尚未创建。",true);return false;}
    GameSproutRuntime::NativeWindow window;
#if defined(__APPLE__)
    window.System=GameSproutRuntime::NativeWindowSystem::Cocoa;
#elif defined(_WIN32)
    window.System=GameSproutRuntime::NativeWindowSystem::Win32;
#else
    window.System=GameSproutRuntime::NativeWindowSystem::X11;
#endif
#if defined(_WIN32)
    window.Handle=reinterpret_cast<std::uintptr_t>(id);
#else
    window.Handle=static_cast<std::uintptr_t>(id);
#endif
    std::string error;_Renderer=_Runtime->CreateViewport(window,_Resources,error);
    if(!_Renderer){DestroyGpu();_Attempted=true;SetStatus(error,true);return false;}
    _NativeID=id;
    return true;
}
void SceneViewport::RenderScene(GameSproutRuntime::IScene* scene,bool grid,float dt,bool continuous,bool animateMaterials) {
    const auto sceneID=scene?scene->GetInstanceID():0;
    if(sceneID!=_SceneID){_SceneID=sceneID;_ViewportChanged=true;_PresentedCamera={};RequestRender();emit FrameStreamReset();}
    if(_Drag!=DragMode::None)AdvanceInput(dt);
    if(!_NeedsRender&&!continuous&&!(animateMaterials&&_AnimatedMaterials))return;
    if(_Rendering||!CanRender())return;
    _NeedsRender=false;
    if(!EnsureRendering())return;
    _Rendering=true;
    struct Guard{bool& Value;~Guard(){Value=false;}} guard{_Rendering};
    GameSproutRuntime::RenderView view;
    view.Scene=scene;view.DrawGrid=grid;
    view.WaitForSurface=false;
    view.ReuseShadowsWhileNavigating=_ReuseShadowsWhileNavigating&&
        (_Drag!=DragMode::None||_WheelNavigationSettle.isActive());
    view.DrawScene=scene!=nullptr;
    view.Camera.View=_Camera.ViewMatrix();view.Camera.Position=_Camera.GetState().Position;
    const auto& camera=_Camera.GetState();
    view.Camera.Projection.VerticalFovRadians=GameSproutRuntime::DegreesToRadians(camera.VerticalFovDegrees);
    view.Camera.Projection.NearPlane=camera.NearPlane;view.Camera.Projection.FarPlane=camera.FarPlane;
    GameSproutRuntime::DebugDrawList debug;
    _Editing->UpdateFrame(scene,&view.Camera);
    if(!_ViewportChanged)_Editing->BuildDraw(scene,debug);
    if(!_ViewportChanged)_Tools->Draw(scene,debug);
    view.DebugDraw=&debug;
    // Runtime Renderer 从 SurfaceFrame 实际像素尺寸算投影，包含 resize / Retina DPI。
    if(!_Renderer->Render(view)){
        _AnimatedMaterials=false;_PresentedCamera={};SetStatus(_Renderer->GetLastError(),true);return;
    }
    const auto& info=_Renderer->GetStatistics();
    if(!info.Width||!info.Height) {
        // The previous image still occupies the surface. Keep its picking
        // camera and retry the pending frame at the next host tick.
        RequestRender();
        return;
    }
    emit FrameSubmitted();
    _Renderer->GetRenderedCamera(_PresentedCamera);
    const bool refreshOverlays=_ViewportChanged;
    _ViewportChanged=false;
    // Initial/changed pixel dimensions need one follow-up to build gizmos from
    // the first successfully presented camera. Later idle frames do no work.
    if(refreshOverlays)RequestRender();
    _AnimatedMaterials=info.RequiresContinuousRendering;
    if(info.Width&&info.Height) {
        std::string message=std::to_string(info.Width)+" × "+std::to_string(info.Height)+" px · "+std::to_string(info.DrawCalls)+" draws · "+std::to_string(info.Triangles)+" triangles · skipped "+std::to_string(info.SkippedObjects);
        if(info.InstancedDrawCalls)message+=" · instanced "+std::to_string(info.InstancedInstances)+" · saved "+std::to_string(info.SavedDrawCalls)+" draws";
        if(info.LayeredShadows)message+=" · static shadow "+std::to_string(info.StaticShadowDrawCalls)+" draws / "+
            std::to_string(info.StaticShadowMapUpdates)+" updates · dynamic shadow "+
            std::to_string(info.DynamicShadowDrawCalls)+" draws / "+std::to_string(info.DynamicShadowMapUpdates)+" updates";
        else if(info.ShadowCascadeCount)message+=" · shadow draws "+std::to_string(info.ShadowDrawCalls)+
            " · cascades "+std::to_string(info.ShadowCascadeCount)+" · shadow updates "+std::to_string(info.ShadowMapUpdates);
        const std::string diagnostic=_Renderer->GetLastError();
        if(!diagnostic.empty())message+=" · "+diagnostic;
        if(!_EditingError.empty())message+=" · "+_EditingError;
        if(!_ColliderDebugError.empty())message+=" · "+_ColliderDebugError;
        SetStatus(message,!diagnostic.empty()||!_EditingError.empty()||!_ColliderDebugError.empty());
    }
}
