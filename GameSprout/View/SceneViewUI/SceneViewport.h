#pragma once
#include "SceneViewCamera.h"
#include "RuntimeRenderApi.h"
#include <QWidget>
#include <QSet>
#include <QPointF>
#include <QTimer>
#include <functional>
class QWindow;
class ISceneManager;
class ISelectionManager;
class IUIAuthoring;
class SceneViewportEditing;
class SceneToolRouter;

class SceneViewport final : public QWidget,public GameSproutRuntime::IRuntimeListener {
    Q_OBJECT
public:
    explicit SceneViewport(QWidget* parent=nullptr);
    ~SceneViewport() override;
    // 也可借用宿主资源提供者；其生命周期须覆盖本 viewport 的渲染生命周期。
    void Initialize(GameSproutRuntime::IGameSproutRuntime* runtime,GameSproutRuntime::IRuntimeResourceProvider* resources);
    void RenderScene(GameSproutRuntime::IScene* scene,bool grid,float deltaSeconds,bool continuous=false,bool animateMaterials=false);
    void RequestRender(){_NeedsRender=true;}
    bool NeedsRender() const{return _NeedsRender;}
    bool IsShadowReuseWhileNavigatingEnabled() const {return _ReuseShadowsWhileNavigating;}
    void SetShadowReuseWhileNavigatingEnabled(bool enabled);
    void OnSceneVisualChanged(std::uint64_t sceneID) override;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent&) override {}
    void Shutdown();
    void RetryRendering();
    GameSproutRuntime::IGameSproutRuntime* GetRuntime() const{return _Runtime;}
    SceneViewCamera& Camera(){return _Camera;}
    const SceneViewCamera& Camera() const{return _Camera;}
    SceneViewportEditing& Editing(){return *_Editing;}
    SceneToolRouter& Tools(){return *_Tools;}
    void BindEditingServices(ISceneManager* scenes,ISelectionManager* selection,IUIAuthoring* uiAuthoring=nullptr);
    GameSproutRuntime::IRuntimeRenderer* Renderer() const{return _Renderer;}
    bool GetRenderedCamera(GameSproutRuntime::RenderedCamera& camera) const;
    std::uint64_t GetPresentedUIPreviewObjectID() const { return _PresentedUIPreviewObjectID; }
    void SetGameUILayoutSize(GameSproutRuntime::Vector2 size);
    GameSproutRuntime::Vector2 GetGameUILayoutSize() const { return _GameUILayoutSize; }
    GameSproutRuntime::Vector2 GetPresentedGameUILayoutSize() const { return _PresentedGameUILayoutSize; }
    void ReportEditingError(const std::string& error){_EditingError=error;SetStatus(error,true);}
    void ClearEditingError(){_EditingError.clear();}
    void SetColliderDebugError(const std::string& error){_ColliderDebugError=error;}
    void ReleaseEditingCapture();
    bool IsRenderingAvailable() const{return _Renderer!=nullptr;}
    const std::string& GetRenderError() const{return _Error;}
    GameSproutRuntime::RenderStatistics GetStatistics() const{return _Renderer?_Renderer->GetStatistics():GameSproutRuntime::RenderStatistics{};}
    std::function<bool()> FocusRequested;
    std::function<void(const std::string&)> StatusChanged;
    static bool IsHeadless();
signals:
    void FrameSubmitted();
    void FrameStreamReset();
protected:
    bool event(QEvent*) override;
    bool eventFilter(QObject*,QEvent*) override;
    QPaintEngine* paintEngine() const override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
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
    SceneToolRouter* _Tools=nullptr;
    DragMode _Drag=DragMode::None;
    Qt::MouseButton _DragButton=Qt::NoButton;
    std::uint64_t _ReleasedTransformPreviewID=0;
    QPointF _LastMouse;
    QSet<int> _Keys;
    QTimer _WheelNavigationSettle;
    QTimer _ResizeSettle;
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
    bool _NeedsRender=true,_RuntimeSubscribed=false;
    bool _AnimatedMaterials=false;
    bool _ReuseShadowsWhileNavigating=true;
    GameSproutRuntime::RenderedCamera _PresentedCamera;
    GameSproutRuntime::Vector2 _GameUILayoutSize,_PresentedGameUILayoutSize;
    GameSproutRuntime::UIDebugDrawList _UIDebugDraw;
    std::uint64_t _PresentedUIPreviewObjectID=0;
    std::uint64_t _SceneID=0;
    std::string _EditingError;
    std::string _ColliderDebugError;
    std::string _Error;
    std::string _Status;
};
