#ifndef __GameViewUI_h__
#define __GameViewUI_h__

#include "Header.h"
#include "IPlayMode.h"
#include "IRuntimeHost.h"
#include "ISceneManager.h"
#include "IAnimationPreview.h"
#include "IRuntimeAssets.h"
#include <QMetaObject>
#include <map>

QT_BEGIN_NAMESPACE
class QWidget;
class QObject;
namespace Ui {
class GameViewUIWindow;
}
QT_END_NAMESPACE

class ISceneManager;
class IRuntimeAssets;
class IAnimationPreview;
class GameViewport;

class GameViewUI : public IGameViewUI, public IRuntimeFrameListener, public ISceneListener, public IAnimationPreviewListener, public IPlayModeListener,public IRuntimeAssetsListener {
public:
    ~GameViewUI() override;

    void OnPlayModeStateChanged(const PlayModeState&) override;
    void Show() override;
    void Hide() override;
    void OnRuntimeFrame(const RuntimeFrameEvent&) override;
    void OnActiveSceneChanged() override;
    void OnAnimationPreviewStateChanged(bool scenePlaying) override;
    void OnAnimationPreviewReset() override;
    void OnClipPreviewStateChanged(std::uint64_t,const GameSproutRuntime::AnimationClipPlaybackState&) override;
    void OnRuntimeResourcesChanged(std::uint64_t) override;
    GameSproutRuntime::Vector2 GetViewportSize() const override;
    bool RegisterViewportSizeCallback(const void*, const fViewportSizeCallback&) override;
    void UnregisterViewportSizeCallback(const void*) override;

private:
    IPlayMode* _PlayMode=nullptr;
    bool _PlaySubscribed=false;
    bool _CanEdit=true;
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Launch(GameSproutEditor::GameSproutViewApi * const editor) override;
    void LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Update(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Release(GameSproutEditor::GameSproutViewApi * const editor) override;
    QWidget* _Window=nullptr;
    QObject* _Dispatch=nullptr;
    Ui::GameViewUIWindow* _Ui=nullptr;
    GameViewport* _Viewport=nullptr;
    std::map<const void*, QMetaObject::Connection> _ViewportSizeConnections;
    IIDEDocker* _Docker=nullptr;
    bool _DockerContentVisible=false,_DockerVisibilitySubscribed=false;
    bool _Loading=false,_LoadingSubscribed=false;
    ISceneManager* _Scenes=nullptr;
    IRuntimeHost* _Host=nullptr;
    GameSproutRuntime::IScene* _ActiveScene=nullptr;
    bool _FrameSubscribed=false,_SceneSubscribed=false,_PreviewSubscribed=false;
    bool _ResourcesSubscribed=false;
    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr;
    IAnimationPreview* _Preview=nullptr;
    IRuntimeAssets* _Resources=nullptr;
    bool _Attached=false;
};
#endif //__GameViewUI_h__
