#include "SceneViewport.h"
#include "SceneViewportEditing.h"
#include "EditorRuntimeResources.h"
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QWindow>
#include <QPaintEngine>
#include <cmath>

bool SceneViewport::IsHeadless(){const QString name=QGuiApplication::platformName();return name=="offscreen"||name.startsWith("minimal");}
SceneViewport::SceneViewport(QWidget* parent):QWidget(parent) {
    _Editing=new SceneViewportEditing(*this);
    if(!IsHeadless()) {
        setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_DontCreateNativeAncestors);
        setAttribute(Qt::WA_PaintOnScreen);
        setAttribute(Qt::WA_NoSystemBackground);
    }
}
SceneViewport::~SceneViewport(){Shutdown();delete _Editing;_Editing=nullptr;}
void SceneViewport::BindEditingServices(ISceneManager* scenes,ISelectionManager* selection){_Editing->Bind(scenes,selection);}
QPaintEngine* SceneViewport::paintEngine() const{return IsHeadless()?QWidget::paintEngine():nullptr;}
void SceneViewport::Initialize(GRHI::GRHIApi* api,GameSproutRuntime::IGameSproutRuntime* runtime,IAssetsManager* assets,ILibraryManager* library) {
    Initialize(api,runtime,static_cast<GameSproutRuntime::IRuntimeResourceProvider*>(nullptr));
    if(assets&&library){_OwnedResources=new EditorRuntimeResources(assets,library);_Resources=_OwnedResources;}
}
void SceneViewport::Initialize(GRHI::GRHIApi* api,GameSproutRuntime::IGameSproutRuntime* runtime,GameSproutRuntime::IRuntimeResourceProvider* resources) {
    Shutdown();_Api=api;_Runtime=runtime;_Initialized=true;_Resources=resources;
    if(IsHeadless())SetStatus("离屏测试环境：保留 Scene 视图与独立相机，不创建原生 GPU 窗口。",true);
    else if(!NativePlatformSupported())SetStatus("当前 Qt 平台尚无兼容的 GRHI 原生窗口句柄。",true);
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
    auto* surface=_Surface;_Surface=nullptr;
    auto* device=_Device;_Device=nullptr;
    _NativeID=0;_Attempted=false;
    _ViewportChanged=true;
    delete renderer;
    if(surface){surface->Release();delete surface;}
    if(device){device->Release();delete device;}
}
void SceneViewport::Shutdown() {
    _Editing->Unbind();
    ClearInput();DestroyGpu();
    if(_NativeWindow){_NativeWindow->removeEventFilter(this);QObject::disconnect(_NativeWindow,nullptr,this,nullptr);_NativeWindow=nullptr;}
    if(_HostWindow){_HostWindow->removeEventFilter(this);QObject::disconnect(_HostWindow,nullptr,this,nullptr);_HostWindow=nullptr;}
    delete _OwnedResources;_OwnedResources=nullptr;_Resources=nullptr;
    _Api=nullptr;_Runtime=nullptr;_Initialized=false;
}
void SceneViewport::RetryRendering(){ClearInput();DestroyGpu();}
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
    else if(event->type()==QEvent::Resize
#if QT_VERSION >= QT_VERSION_CHECK(6,6,0)
            ||event->type()==QEvent::DevicePixelRatioChange
#endif
    ){ClearInput();_ViewportChanged=true;}
    else if(event->type()==QEvent::Leave&&!_Editing->IsDragging())_Editing->Move({-100,-100},false);
    else if(event->type()==QEvent::WindowDeactivate||event->type()==QEvent::UngrabMouse)ClearInput();
    return QWidget::event(event);
}
bool SceneViewport::eventFilter(QObject* watched,QEvent* event) {
    if((watched==_NativeWindow||watched==_HostWindow) && event->type()==QEvent::PlatformSurface) {
        const auto kind=static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType();
        if(kind==QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed){ClearInput();DestroyGpu();}
        else if(kind==QPlatformSurfaceEvent::SurfaceCreated)_Attempted=false;
    }
    return QWidget::eventFilter(watched,event);
}
bool SceneViewport::CanRender() const {
    if(!_Initialized || IsHeadless() || !NativePlatformSupported() || !isVisible() || width()<=0 || height()<=0)return false;
    const QWidget* host=window();
    return host && !host->isMinimized() && host->windowHandle() && host->windowHandle()->isExposed();
}
bool SceneViewport::EnsureRendering() {
    if(_Renderer&&_Surface)return true;
    if(_Attempted)return false;
    _Attempted=true;
    if(!_Api||!_Runtime||!_Resources){SetStatus("Scene 渲染依赖的 GRHI、Runtime 或资源服务尚未就绪。",true);return false;}
    // 只在真实、已显示的原生平台调用 winId，绝不把 offscreen/minimal 的伪 ID 交给 Cocoa。
    const WId id=winId();ObserveNativeWindow();
    _Attempted=true;
    if(!id){SetStatus("Scene 原生渲染窗口尚未创建。",true);return false;}
    _Device=_Api->CreateDevice();
    if(!_Device){SetStatus(_Api->GetLastError(),true);return false;}
#if defined(_WIN32) || defined(__APPLE__)
    const auto handle=reinterpret_cast<GRHI::GRHIWindowHandle>(id);
#else
    const auto handle=static_cast<GRHI::GRHIWindowHandle>(id);
#endif
    _Surface=_Device->CreateSurface(handle);
    if(!_Surface){const std::string error=_Device->GetLastError();DestroyGpu();_Attempted=true;SetStatus(error,true);return false;}
    _NativeID=id;
    _Renderer=_Runtime->CreateRenderer(_Device,_Resources);
    if(!_Renderer){DestroyGpu();_Attempted=true;SetStatus("Runtime 无法创建 Scene Renderer。",true);return false;}
    return true;
}
void SceneViewport::RenderScene(GameSproutRuntime::IScene* scene,bool grid,float dt) {
    AdvanceInput(dt);
    if(!scene||!CanRender()||_Rendering||!EnsureRendering()){_Editing->UpdateFrame(scene);return;}
    _Rendering=true;
    struct Guard{bool& Value;~Guard(){Value=false;}} guard{_Rendering};
    GameSproutRuntime::RenderView view;
    view.Scene=scene;view.Surface=_Surface;view.DrawGrid=grid;
    view.Camera.View=_Camera.ViewMatrix();view.Camera.Position=_Camera.GetState().Position;
    const auto& camera=_Camera.GetState();
    view.Camera.Projection.VerticalFovRadians=GameSproutRuntime::DegreesToRadians(camera.VerticalFovDegrees);
    view.Camera.Projection.NearPlane=camera.NearPlane;view.Camera.Projection.FarPlane=camera.FarPlane;
    GameSproutRuntime::DebugDrawList debug;
    _Editing->UpdateFrame(scene,&view.Camera);
    if(!_ViewportChanged)_Editing->BuildDraw(scene,debug);
    view.DebugDraw=&debug;
    // Runtime Renderer 从 SurfaceFrame 实际像素尺寸算投影，包含 resize / Retina DPI。
    if(!_Renderer->Render(view)){SetStatus(_Renderer->GetLastError(),true);return;}
    _ViewportChanged=false;
    const auto& info=_Renderer->GetStatistics();
    if(info.Width&&info.Height) {
        std::string message=std::to_string(info.Width)+" × "+std::to_string(info.Height)+" px · "+std::to_string(info.DrawCalls)+" draws · "+std::to_string(info.Triangles)+" triangles · skipped "+std::to_string(info.SkippedObjects);
        const std::string diagnostic=_Renderer->GetLastError();
        if(!diagnostic.empty())message+=" · "+diagnostic;
        if(!_EditingError.empty())message+=" · "+_EditingError;
        SetStatus(message,!diagnostic.empty());
    }
}
