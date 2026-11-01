#pragma once
#include "RuntimeRenderApi.h"
#include <QWidget>
#include <functional>
class QWindow;

// Native window hosting only; AnimationPreview owns the shared animation clock.
class GameViewport final : public QWidget {
    Q_OBJECT
public:
    explicit GameViewport(QWidget* parent=nullptr);
    ~GameViewport() override;
    void Initialize(GameSproutRuntime::IGameSproutRuntime*,GameSproutRuntime::IRuntimeResourceProvider*);
    void RenderGame(GameSproutRuntime::IScene*);
    void Shutdown();
    void RetryRendering();
    bool IsRenderingAvailable() const {return _Renderer!=nullptr;}
    std::function<void(const std::string&)> StatusChanged;
protected:
    bool event(QEvent*) override;
    bool eventFilter(QObject*,QEvent*) override;
    QPaintEngine* paintEngine() const override;
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
    bool _Initialized=false;
    bool _Attempted=false;
    bool _Rendering=false;
    std::string _Status;
};
