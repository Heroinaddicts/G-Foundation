#pragma once
#include "RuntimeRenderApi.h"
#include <QWidget>
#include <functional>
class QWindow;

// Native viewport and focused gameplay input adapter; RuntimeHost owns all clocks.
class GameViewport final : public QWidget,public GameSproutRuntime::IRuntimeListener {
    Q_OBJECT
public:
    explicit GameViewport(QWidget* parent=nullptr);
    ~GameViewport() override;
    void Initialize(GameSproutRuntime::IGameSproutRuntime*,GameSproutRuntime::IRuntimeResourceProvider*);
    void RenderGame(GameSproutRuntime::IScene*,bool continuous=false,bool animateMaterials=false);
    void RequestRender(){_NeedsRender=true;}
    bool NeedsRender() const{return _NeedsRender;}
    void OnSceneVisualChanged(std::uint64_t sceneID) override;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent&) override {}
    void Shutdown();
    void RetryRendering();
    void SetGameplayInputEnabled(bool enabled);
    void ReleaseGameInput();
    bool IsRenderingAvailable() const {return _Renderer!=nullptr;}
    std::function<void(const std::string&)> StatusChanged;
signals:
    void FrameSubmitted();
    void FrameStreamReset();
protected:
    bool event(QEvent*) override;
    bool eventFilter(QObject*,QEvent*) override;
    QPaintEngine* paintEngine() const override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void focusInEvent(QFocusEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    void hideEvent(QHideEvent*) override;
    bool focusNextPrevChild(bool next) override;
private:
    bool EnsureRendering();
    void DestroyGPU();
    void ObserveNativeWindow();
    void Status(const std::string&);
    static bool Headless();
    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr;
    GameSproutRuntime::IRuntimeResourceProvider* _Resources=nullptr;
    GameSproutRuntime::IRuntimeViewport* _Renderer=nullptr;
    QWindow* _NativeWindow=nullptr;
    QWindow* _HostWindow=nullptr;
    bool _GameplayInputEnabled=false;
    bool _InputCaptured=false;
    bool _Initialized=false;
    bool _Attempted=false;
    bool _Rendering=false;
    bool _NeedsRender=true,_RuntimeSubscribed=false;
    bool _AnimatedMaterials=false;
    std::uint64_t _SceneID=0;
    std::string _Status;
};
