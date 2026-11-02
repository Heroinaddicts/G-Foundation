#ifndef __SceneViewUI_h__
#define __SceneViewUI_h__

#include "Header.h"
#include "IPlayMode.h"
#include <QElapsedTimer>
#include "IRuntimeHost.h"
#include "ISceneManager.h"

QT_BEGIN_NAMESPACE
class QMainWindow;
namespace Ui {
class SceneViewUIWindow;
}
QT_END_NAMESPACE
class ISceneManager;
class ISelectionManager;
class IRuntimeAssets;
class SceneViewport;
class SceneViewChrome;

class SceneViewUI : public ISceneViewUI, public IRuntimeFrameListener, public ISceneListener, public IPlayModeListener {
public:
    ~SceneViewUI() override;

    void OnPlayModeStateChanged(const PlayModeState&) override;
    void Show() override;
    void Hide() override;
    void OnRuntimeFrame(const RuntimeFrameEvent&) override;
    void OnActiveSceneChanged() override;
    SceneViewCameraState GetCameraState() const override;
    bool SetCameraState(const SceneViewCameraState& state) override;
    void ResetCamera() override;
    bool FocusSelection() override;
    bool IsRenderingAvailable() const override;
    std::string GetRenderError() const override;
    bool RegisterSceneTool(const void*,ISceneTool*) override;
    bool ActivateSceneTool(ISceneTool*) override;
    void DeactivateSceneTool(ISceneTool*) override;
    ISceneTool* GetActiveSceneTool() const override;
    void UnregisterSceneTools(const void*) override;

private:
    IPlayMode* _PlayMode=nullptr;
    bool _PlaySubscribed=false;
    bool _CanEdit=true;
    bool _ToolPlayStateKnown=false;
    EditorPlayState _ToolPlayState=EditorPlayState::Edit;
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Launch(GameSproutEditor::GameSproutViewApi * const editor) override;
    void LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Update(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Release(GameSproutEditor::GameSproutViewApi * const editor) override;
    QWidget* _Window=nullptr;
    QObject* _Dispatch=nullptr;
    Ui::SceneViewUIWindow* _Ui=nullptr;
    SceneViewport* _Viewport=nullptr;
    SceneViewChrome* _Chrome=nullptr;
    IIDEDocker* _Docker=nullptr;
    bool _DockerContentVisible=false,_DockerVisibilitySubscribed=false;
    ISceneManager* _Scenes=nullptr;
    IRuntimeHost* _Host=nullptr;
    GameSproutRuntime::IScene* _ActiveScene=nullptr;
    bool _FrameSubscribed=false,_SceneSubscribed=false;
    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr;
    ISelectionManager* _Selection=nullptr;
    IRuntimeAssets* _RuntimeAssets=nullptr;
    bool _Attached=false;
    QElapsedTimer _Timer;
};
#endif //__SceneViewUI_h__
