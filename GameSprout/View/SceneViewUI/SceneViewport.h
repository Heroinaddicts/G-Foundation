#pragma once
#include "SceneViewCamera.h"
#include "RuntimeRenderApi.h"
#include <QWidget>
#include <QSet>
#include <QPointF>
#include <functional>
class QWindow;
class ISceneManager;
class ISelectionManager;
class SceneViewportEditing;

class SceneViewport final : public QWidget {
    Q_OBJECT
public:
    explicit SceneViewport(QWidget* parent=nullptr);
    ~SceneViewport() override;
    // 也可借用宿主资源提供者；其生命周期须覆盖本 viewport 的渲染生命周期。
    void Initialize(GameSproutRuntime::IGameSproutRuntime* runtime,GameSproutRuntime::IRuntimeResourceProvider* resources);
    void RenderScene(GameSproutRuntime::IScene* scene,bool grid,float deltaSeconds);
    void Shutdown();
    void RetryRendering();
    GameSproutRuntime::IGameSproutRuntime* GetRuntime() const{return _Runtime;}
    SceneViewCamera& Camera(){return _Camera;}
    const SceneViewCamera& Camera() const{return _Camera;}
    SceneViewportEditing& Editing(){return *_Editing;}
    void BindEditingServices(ISceneManager* scenes,ISelectionManager* selection);
    GameSproutRuntime::IRuntimeRenderer* Renderer() const{return _Renderer;}
    bool GetRenderedCamera(GameSproutRuntime::RenderedCamera& camera) const{return !_ViewportChanged&&_Renderer&&_Renderer->GetRenderedCamera(camera);}
    void ReportEditingError(const std::string& error){_EditingError=error;SetStatus(error);}
    void ClearEditingError(){_EditingError.clear();}
    void ReleaseEditingCapture();
    bool IsRenderingAvailable() const{return _Renderer!=nullptr;}
    const std::string& GetRenderError() const{return _Error;}
    GameSproutRuntime::RenderStatistics GetStatistics() const{return _Renderer?_Renderer->GetStatistics():GameSproutRuntime::RenderStatistics{};}
    std::function<bool()> FocusRequested;
    std::function<void(const std::string&)> StatusChanged;
    static bool IsHeadless();
protected:
    bool event(QEvent*) override;
    bool eventFilter(QObject*,QEvent*) override;
    QPaintEngine* paintEngine() const override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    void hideEvent(QHideEvent*) override;
private:
    enum class DragMode {None,Look,Orbit,Pan};
    void ClearInput();
    void AdvanceInput(float dt);
    bool NativePlatformSupported() const;
    bool CanRender() const;
    bool EnsureRendering();
    void ObserveNativeWindow();
    void DestroyGpu();
    void SetStatus(const std::string& message,bool error=false);
    SceneViewCamera _Camera;
    SceneViewportEditing* _Editing=nullptr;
    DragMode _Drag=DragMode::None;
    Qt::MouseButton _DragButton=Qt::NoButton;
    QPointF _LastMouse;
    QSet<int> _Keys;
    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr;
    GameSproutRuntime::IRuntimeResourceProvider* _Resources=nullptr;
    GameSproutRuntime::IRuntimeViewport* _Renderer=nullptr;
    QWindow* _NativeWindow=nullptr;
    QWindow* _HostWindow=nullptr;
    WId _NativeID=0;
    bool _Initialized=false;
    bool _Attempted=false;
    bool _Rendering=false;
    bool _ViewportChanged=true;
    std::string _EditingError;
    std::string _Error;
    std::string _Status;
};
