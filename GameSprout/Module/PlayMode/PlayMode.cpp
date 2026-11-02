#include "PlayMode.h"
#include "ISceneManager.h"
#include "IRuntimeHost.h"
#include "IAnimationPreview.h"
#include <QTimer>
#include <QDebug>
#include <exception>
#include <limits>

namespace {
class TransitionScope {
public:
    explicit TransitionScope(bool& changing) : _Changing(changing) { _Changing=true; }
    ~TransitionScope() { _Changing=false; }
private:
    bool& _Changing;
};
}
PlayMode::~PlayMode() { Release(nullptr); }
void PlayMode::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api) {
    if(!api||!api->_EditorApi) return;
    auto* editor=api->_EditorApi;
    _Scenes=editor->GetGameSproutModule<ISceneManager>("SceneManager");
    _Host=editor->GetGameSproutModule<IRuntimeHost>("RuntimeHost");
    _Preview=editor->GetGameSproutModule<IAnimationPreview>("AnimationPreview");
    _Scripts=editor->GetGameSproutModule<IScriptEditor>("ScriptEditor");
    _Runtime=editor->_Runtime;
}
bool PlayMode::Initialize(GameSproutEditor::GameSproutModuleApi*) {
    return _Scenes&&_Host&&_Preview&&_Scripts&&_Runtime;
}
bool PlayMode::Launch(GameSproutEditor::GameSproutModuleApi*) {
    if(_Launched) return true;
    if(!_Scripts) return false;
    _Dispatch=new QObject();
    _Subscribed=_Scripts->RegisterScriptListener(this);
    if(!_Subscribed) {delete _Dispatch;_Dispatch=nullptr;return false;}
    _Launched=true;return true;
}
bool PlayMode::Ready(std::string& error) const {
    error.clear();
    if(!_Runtime||!_Scenes||!_Host||!_Scripts) {error="运行模式服务尚未就绪。";return false;}
    if(_Changing||_Notifying||_Runtime->IsExecuting()||_Host->IsFrameActive()) {error="正在执行回调或模式切换，请在回调结束后再操作。";return false;}
    return true;
}
void PlayMode::SetState(EditorPlayState state,const std::string& error) {
    if(_State.State==state&&_State.Error==error) return;
    _State={state,error};
    const auto snapshot=_State;
    const uint64_t last=_NextListener;
    _Notifying=true;
    auto current=_Listeners.begin();
    while(current!=_Listeners.end()&&current->first<=last) {
        const uint64_t id=current->first;
        try {current->second->OnPlayModeStateChanged(snapshot);}
        catch(...) {qWarning("Play mode listener threw an exception");}
        current=_Listeners.upper_bound(id);
    }
    _Notifying=false;
    if(!error.empty()) qWarning().noquote()<<QString::fromStdString(error);
}
bool PlayMode::RegisterPlayModeListener(IPlayModeListener* listener) {
    if(!listener||_NextListener==std::numeric_limits<uint64_t>::max()) return false;
    for(const auto& entry:_Listeners) if(entry.second==listener) return false;
    _Listeners.emplace(++_NextListener,listener);return true;
}
void PlayMode::UnregisterPlayModeListener(IPlayModeListener* listener) {
    for(auto item=_Listeners.begin();item!=_Listeners.end();)
        if(item->second==listener) item=_Listeners.erase(item);else ++item;
}
bool PlayMode::Play(std::string& error) {
    if(!Ready(error)) return false;
    if(!_Launched||_State.State!=EditorPlayState::Edit) {error="只有编辑模式可以开始运行。";return false;}
    if(!_Scenes->GetActiveScene()) {error="当前没有可运行的场景。";return false;}
    if(_Scenes->HasTransformEdit()) {error="请先完成或取消 Scene 中的变换拖拽。";return false;}
    TransitionScope transition(_Changing);
    _WantPlay=true;SetState(EditorPlayState::EnteringPlay);
    // Revalidate the known current project sources through the incremental
    // compiler. Source revisions arriving during preparation supersede its result.
    if(!_Scripts->RequestCompile(error)) {
        _WantPlay=false;SetState(EditorPlayState::Edit,error);return false;
    }
    QueueEnter();return true;
}
void PlayMode::QueueEnter() {
    if(!_Dispatch||_Queued||!_WantPlay) return;
    _Queued=true;
    // Never rebuild a world on the ScriptEditor listener/load stack.
    QTimer::singleShot(0,_Dispatch,[this] {
        _Queued=false;
        if(_Launched&&_WantPlay&&_State.State==EditorPlayState::EnteringPlay) ContinueEnter();
    });
}
void PlayMode::OnScriptBuildStateChanged(const ScriptBuildState&) { QueueEnter(); }
void PlayMode::FailEnter(const std::string& reason) {
    _WantPlay=false;
    _Session=_Session||_Scenes->IsPlaySessionActive();
    if(_Host->IsGameplayActive()) _Host->EndGameplay();
    std::string error=reason,cleanup;
    if(_Host->IsGameplayActive()) {
        SetState(EditorPlayState::ExitingPlay,error+"\n游戏帧尚未结束，请重试停止。");return;
    }
    if(_Session) {
        if(!_Scenes->EndPlaySession(cleanup)) {
            SetState(EditorPlayState::ExitingPlay,error+"\n恢复编辑场景失败："+cleanup);return;
        }
        _Session=false;
    }
    if(_ReloadLocked) {
        if(!_Scripts->SetReloadBlocked(false,cleanup)) {
            SetState(EditorPlayState::ExitingPlay,error+"\n"+cleanup);return;
        }
        _ReloadLocked=false;
    }
    SetState(EditorPlayState::Edit,error);
}
void PlayMode::ContinueEnter() {
    if(_Changing||_Runtime->IsExecuting()||_Host->IsFrameActive()) {QueueEnter();return;}
    const auto build=_Scripts->GetBuildState();
    if(build.Compiling) return;
    if(!build.Error.empty()) {FailEnter("无法开始运行："+build.Error);return;}
    if(build.PendingChanges) return;
    TransitionScope transition(_Changing);
    std::string error;
    try {
        if(!_Scripts->SetReloadBlocked(true,error)) {FailEnter(error);return;}
        _ReloadLocked=true;
        _Preview->ResetAnimationPreview();
        _Host->SetSimulationPlaying(false);
        if(!_Scenes->BeginPlaySession(error)) {FailEnter(error);return;}
        _Session=true;
        if(!_Host->BeginGameplay(error)) {FailEnter(error);return;}
        _WantPlay=false;SetState(EditorPlayState::Playing);
    } catch(const std::exception& exception) {FailEnter(exception.what());}
      catch(...) {FailEnter("开始游戏运行时发生未知错误。");}
}
bool PlayMode::Pause(std::string& error) {
    if(!Ready(error)) return false;
    if(_State.State!=EditorPlayState::Playing) {error="只有运行中可以暂停。";return false;}
    TransitionScope transition(_Changing);
    _Host->SetGameplayPaused(true);
    if(!_Host->IsGameplayPaused()) {error="游戏帧尚未结束，无法暂停。";return false;}
    SetState(EditorPlayState::Paused);return true;
}
bool PlayMode::Resume(std::string& error) {
    if(!Ready(error)) return false;
    if(_State.State!=EditorPlayState::Paused) {error="只有暂停时可以继续运行。";return false;}
    TransitionScope transition(_Changing);
    _Host->SetGameplayPaused(false);
    if(_Host->IsGameplayPaused()) {error="游戏帧尚未结束，无法恢复。";return false;}
    SetState(EditorPlayState::Playing);return true;
}
bool PlayMode::Step(std::string& error) {
    if(!Ready(error)) return false;
    if(_State.State!=EditorPlayState::Paused) {error="请先暂停游戏再单步执行。";return false;}
    return _Host->StepGameplay(error);
}
bool PlayMode::Stop(std::string& error) {
    if(!Ready(error)) return false;
    if(_State.State==EditorPlayState::Edit&&!_Session&&!_ReloadLocked) return true;
    TransitionScope transition(_Changing);
    _WantPlay=false;
    SetState(EditorPlayState::ExitingPlay);
    _Host->EndGameplay();
    if(_Host->IsGameplayActive()) {error="游戏帧尚未结束，无法停止。";SetState(EditorPlayState::ExitingPlay,error);return false;}
    if(_Session) {
        if(!_Scenes->EndPlaySession(error)) {SetState(EditorPlayState::ExitingPlay,error);return false;}
        _Session=false;
    }
    if(_ReloadLocked) {
        if(!_Scripts->SetReloadBlocked(false,error)) {SetState(EditorPlayState::ExitingPlay,error);return false;}
        _ReloadLocked=false;
    }
    SetState(EditorPlayState::Edit);return true;
}
void PlayMode::Release(GameSproutEditor::GameSproutModuleApi*) {
    _Launched=false;_WantPlay=false;
    delete _Dispatch;_Dispatch=nullptr;_Queued=false;
    if(_Subscribed&&_Scripts) _Scripts->UnregisterScriptListener(this);
    _Subscribed=false;
    if(_Host&&_Scenes&&_Scripts&&_Runtime) {
        std::string error;
        if(!Stop(error)) {qWarning().noquote()<<"Cannot stop play session:"<<QString::fromStdString(error);return;}
    }
    _Listeners.clear();
    _Scenes=nullptr;_Host=nullptr;_Preview=nullptr;_Scripts=nullptr;_Runtime=nullptr;
}
