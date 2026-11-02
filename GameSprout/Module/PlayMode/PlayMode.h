#pragma once
#include "IPlayMode.h"
#include "IScriptEditor.h"
#include <QObject>
#include <map>

class ISceneManager;
class IRuntimeHost;
class IAnimationPreview;

class PlayMode final : public IPlayMode, public IScriptEditorListener {
public:
    ~PlayMode() override;
    PlayModeState GetState() const override { return _State; }
    bool Play(std::string&) override;
    bool Pause(std::string&) override;
    bool Resume(std::string&) override;
    bool Step(std::string&) override;
    bool Stop(std::string&) override;
    bool RegisterPlayModeListener(IPlayModeListener*) override;
    void UnregisterPlayModeListener(IPlayModeListener*) override;
    void OnScriptBuildStateChanged(const ScriptBuildState&) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override;
    void Release(GameSproutEditor::GameSproutModuleApi*) override;
    bool Ready(std::string&) const;
    void SetState(EditorPlayState,const std::string& error = {});
    void QueueEnter();
    void ContinueEnter();
    void FailEnter(const std::string&);
    ISceneManager* _Scenes = nullptr;
    IRuntimeHost* _Host = nullptr;
    IAnimationPreview* _Preview = nullptr;
    IScriptEditor* _Scripts = nullptr;
    GameSproutRuntime::IGameSproutRuntime* _Runtime = nullptr;
    QObject* _Dispatch = nullptr;
    PlayModeState _State;
    bool _Launched = false, _Subscribed = false, _Queued = false;
    bool _WantPlay = false, _Changing = false, _Notifying = false;
    bool _Session = false, _ReloadLocked = false;
    uint64_t _NextListener = 0;
    std::map<uint64_t,IPlayModeListener*> _Listeners;
};
