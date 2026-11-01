#include "GameViewport.h"
#include <QGuiApplication>
#include <QPaintEngine>
#include <QPlatformSurfaceEvent>
#include <QWindow>

bool GameViewport::Headless() {
    const auto platform=QGuiApplication::platformName();
    return platform=="offscreen"||platform.startsWith("minimal");
}
GameViewport::GameViewport(QWidget* parent):QWidget(parent) {
    if (!Headless()) {
        setAttribute(Qt::WA_NativeWindow);setAttribute(Qt::WA_DontCreateNativeAncestors);
        setAttribute(Qt::WA_PaintOnScreen);setAttribute(Qt::WA_NoSystemBackground);
    }
}
GameViewport::~GameViewport(){Shutdown();}
QPaintEngine* GameViewport::paintEngine() const {return Headless()?QWidget::paintEngine():nullptr;}
void GameViewport::Initialize(GameSproutRuntime::IGameSproutRuntime* runtime,
    GameSproutRuntime::IRuntimeResourceProvider* resources) {
    Shutdown();_Runtime=runtime;_Resources=resources;_Initialized=true;
}
void GameViewport::Status(const std::string& text) {
    if (_Status==text) return;
    _Status=text;if(StatusChanged)StatusChanged(text);
}
void GameViewport::DestroyGPU() {
    auto* renderer=_Renderer;_Renderer=nullptr;
    _Attempted=false;
    delete renderer;
}
void GameViewport::Shutdown(){
    _Initialized=false;DestroyGPU();
    if(_NativeWindow){_NativeWindow->removeEventFilter(this);QObject::disconnect(_NativeWindow,nullptr,this,nullptr);_NativeWindow=nullptr;}
    if(_HostWindow){_HostWindow->removeEventFilter(this);QObject::disconnect(_HostWindow,nullptr,this,nullptr);_HostWindow=nullptr;}
    _Runtime=nullptr;_Resources=nullptr;
}
void GameViewport::RetryRendering(){DestroyGPU();_Attempted=false;}
void GameViewport::ObserveNativeWindow() {
    QWindow* native=windowHandle();
    if(native!=_NativeWindow){
        if(_NativeWindow){_NativeWindow->removeEventFilter(this);QObject::disconnect(_NativeWindow,nullptr,this,nullptr);}
        _NativeWindow=native;
        if(native){native->installEventFilter(this);QObject::connect(native,&QObject::destroyed,this,[this,native]{if(_NativeWindow==native)_NativeWindow=nullptr;});}
    }
    QWindow* host=window()?window()->windowHandle():nullptr;
    if(host==native)host=nullptr;
    if(host!=_HostWindow){
        if(_HostWindow){_HostWindow->removeEventFilter(this);QObject::disconnect(_HostWindow,nullptr,this,nullptr);}
        _HostWindow=host;
        if(host){host->installEventFilter(this);QObject::connect(host,&QObject::destroyed,this,[this,host]{if(_HostWindow==host)_HostWindow=nullptr;});}
    }
}
bool GameViewport::event(QEvent* event) {
    if (event->type()==QEvent::WinIdChange) {RetryRendering();if(_Initialized)ObserveNativeWindow();}
    if (event->type()==QEvent::PlatformSurface) {
        const auto type=static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType();
        if(type==QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)DestroyGPU();
        _Attempted=false;
    }
    return QWidget::event(event);
}
bool GameViewport::eventFilter(QObject* watched,QEvent* event) {
    if((watched==_NativeWindow||watched==_HostWindow)&&event->type()==QEvent::PlatformSurface){
        const auto type=static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType();
        // Native QWindow receives this before its platform handle is destroyed, including host teardown/reparenting.
        if(type==QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)DestroyGPU();
        else if(type==QPlatformSurfaceEvent::SurfaceCreated)_Attempted=false;
    }
    return QWidget::eventFilter(watched,event);
}
bool GameViewport::EnsureRendering() {
    if (_Renderer) return true;
    if (_Attempted) return false;
    _Attempted=true;
    if (!_Runtime||!_Resources) {Status("Game 渲染服务尚未就绪。");return false;}
    const auto platform=QGuiApplication::platformName();
#if defined(__APPLE__)
    const bool supported=platform=="cocoa";
#elif defined(_WIN32)
    const bool supported=platform=="windows";
#else
    const bool supported=platform=="xcb";
#endif
    if (!supported) {Status("当前平台没有可用的 Game 原生窗口句柄。");return false;}
    const WId id=winId();ObserveNativeWindow();_Attempted=true;
    if(!id){Status("Game 原生窗口尚未创建。");return false;}
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
    if(!_Renderer){DestroyGPU();_Attempted=true;Status(error);return false;}
    return true;
}
void GameViewport::RenderGame(GameSproutRuntime::IScene* scene) {
    if (!_Initialized||!isVisible()||width()<=0||height()<=0||_Rendering) return;
    GameSproutRuntime::RenderView view;
    view.Scene=scene;view.DrawGrid=false;
    std::string diagnostic;
    auto* camera=scene?scene->GetMainCamera():nullptr;
    if(!scene)diagnostic="尚无活动场景。";
    else if(!camera||!camera->GetGameObject()->IsActiveInHierarchy())diagnostic="场景中没有启用的 Main Camera。";
    else if(!camera->GetViewMatrix(view.Camera.View))diagnostic="Main Camera 变换不可逆。";
    else {
        view.Camera.Projection=camera->GetSettings();
        const auto world=camera->GetGameObject()->GetTransform()->GetWorldMatrix();
        view.Camera.Position={world.Data[12],world.Data[13],world.Data[14]};
    }
    // Runtime owns target clearing as well as scene drawing; the View never records content commands.
    view.DrawScene=diagnostic.empty();
    if (Headless()) {Status(diagnostic.empty()?"离屏环境：Game 使用 Main Camera，与 Scene 共用动画姿态。":diagnostic);return;}
    if (!window()||window()->isMinimized()||!window()->windowHandle()||!window()->windowHandle()->isExposed()) return;
    if (!EnsureRendering()) return;
    _Rendering=true;
    struct Scope{bool& Flag;~Scope(){Flag=false;}} scope{_Rendering};
    if (!_Renderer->Render(view)) {Status(_Renderer->GetLastError());return;}
    if(!diagnostic.empty()){Status(diagnostic);return;}
    const auto& statistics=_Renderer->GetStatistics();
    Status(std::to_string(statistics.Width)+" × "+std::to_string(statistics.Height)+" px · "+
        std::to_string(statistics.DrawCalls)+" draws · "+std::to_string(statistics.Triangles)+" triangles");
}
