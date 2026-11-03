#include "RuntimeHost.h"
#include "IRuntimeAssets.h"
#include <QDebug>
#include <limits>
#include <algorithm>
#include <cmath>

namespace {
class HostTransition {
public:
    explicit HostTransition(bool& changing):_Changing(changing){_Changing=true;}
    ~HostTransition(){_Changing=false;}
private:
    bool& _Changing;
};
}

RuntimeHost::~RuntimeHost(){Release(nullptr);}
void RuntimeHost::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api){
    _Resources=api&&api->_EditorApi?api->_EditorApi->GetGameSproutModule<IRuntimeAssets>("RuntimeAssets"):nullptr;
}
bool RuntimeHost::Initialize(GameSproutEditor::GameSproutModuleApi* api){
    _Runtime=api&&api->_EditorApi?api->_EditorApi->_Runtime:nullptr;
    if(!_Runtime)qCritical()<<"EditorApi has no Runtime instance.";
    return _Runtime!=nullptr;
}
bool RuntimeHost::Launch(GameSproutEditor::GameSproutModuleApi*){
    if(!_Runtime)return false;
    _Runtime->SetResourceProvider(_Resources?_Resources->GetResourceProvider():nullptr);
    _Playing=false;_Timer.start();return true;
}
void RuntimeHost::SetSimulationPlaying(bool playing){
    if(_Gameplay||_Changing)return;
    if(_Playing==playing)return;
    HostTransition transition(_Changing);
    _Playing=playing;ResetClock();
    NotifyPlayingChanged();
}
bool RuntimeHost::BeginGameplay(std::string& error){
    error.clear();
    if(!_Runtime||_Gameplay||IsFrameActive()||_Runtime->IsExecuting()) {error="Runtime host is busy or gameplay is already active";return false;}
    HostTransition transition(_Changing);
    if(!_Runtime->BeginExecution(error))return false;
    _Gameplay=true;_Paused=false;_StepPending=false;_FixedAccumulator=0;
    _Playing=true;ResetClock();NotifyPlayingChanged();return true;
}
void RuntimeHost::EndGameplay(){
    if(!_Gameplay||!_Runtime||IsFrameActive()||_Runtime->IsExecuting())return;
    HostTransition transition(_Changing);
    std::string error;
    if(!_Runtime->EndExecution(error)){qWarning().noquote()<<QString::fromStdString(error);return;}
    _Gameplay=false;_Paused=false;_StepPending=false;_FixedAccumulator=0;
    _Playing=false;ResetClock();NotifyPlayingChanged();
}
void RuntimeHost::SetGameplayPaused(bool paused){
    if(!_Gameplay||_Paused==paused||IsFrameActive()||!_Runtime||_Runtime->IsExecuting())return;
    HostTransition transition(_Changing);
    _Paused=paused;_StepPending=false;_FixedAccumulator=0;
    _Runtime->SetInputEnabled(!paused);
    _Playing=!paused;ResetClock();NotifyPlayingChanged();
}
bool RuntimeHost::StepGameplay(std::string& error){
    error.clear();
    if(!_Runtime||!_Gameplay||!_Paused||IsFrameActive()||_Runtime->IsExecuting()||_StepPending){error="Single step requires an idle paused gameplay session";return false;}
    _StepPending=true;ResetClock();return true;
}
void RuntimeHost::ResetClock(){_Timer.start();}
void RuntimeHost::Update(GameSproutEditor::GameSproutModuleApi*){
    if(!_Runtime||IsFrameActive())return;
    const double elapsed=_Timer.isValid()?_Timer.nsecsElapsed()/1000000000.0:0.0;
    _Timer.start();
    struct TickScope {bool& Flag;explicit TickScope(bool& flag):Flag(flag){Flag=true;}~TickScope(){Flag=false;}} ticking(_Ticking);
    constexpr double fixedSeconds=1.0/60.0;
    float delta=0;
    if(_Gameplay){
        const bool step=_StepPending;_StepPending=false;
        if(step){
            delta=static_cast<float>(fixedSeconds);
            _Runtime->AdvanceFrame(delta,1,delta);
        }else if(!_Paused){
            // Bound debugger stalls/import delays and avoid an unbounded catch-up loop.
            delta=static_cast<float>(std::clamp(elapsed,0.0,0.1));
            _FixedAccumulator+=delta;
            unsigned steps=0;
            while(_FixedAccumulator+1e-9>=fixedSeconds&&steps<6){
                ++steps;_FixedAccumulator-=fixedSeconds;
            }
            _FixedAccumulator=std::max(0.0,_FixedAccumulator);
            // Runtime owns the complete phase barrier, including Early before
            // all fixed steps and Late before either viewport observes the frame.
            _Runtime->AdvanceFrame(delta,steps,static_cast<float>(fixedSeconds));
        }else _Runtime->ProcessPendingEvents();
    }else{
        delta=_Playing?static_cast<float>(std::clamp(elapsed,0.0,0.1)):0.f;
        _Runtime->AdvanceFrame(delta,0,0);
    }
    _Runtime->CompleteInputFrame();
    NotifyFrame({++_FrameNumber,delta,_Playing,_Gameplay,_Paused});
    // All views share this frame. Collect unattached objects only after every
    // frame listener has finished, including edit and paused gameplay frames.
    std::string frameError;
    if(!_Runtime->EndFrame(frameError))qWarning().noquote()<<QString::fromStdString(frameError);
}
bool RuntimeHost::RegisterFrameListener(IRuntimeFrameListener* listener){
    if(!listener||_NextListener==std::numeric_limits<uint64_t>::max())return false;
    for(const auto& entry:_Listeners)if(entry.second==listener)return false;
    _Listeners.emplace(++_NextListener,listener);return true;
}
void RuntimeHost::UnregisterFrameListener(IRuntimeFrameListener* listener){
    for(auto current=_Listeners.begin();current!=_Listeners.end();)
        if(current->second==listener)current=_Listeners.erase(current);else ++current;
}
void RuntimeHost::NotifyFrame(const RuntimeFrameEvent& event){
    const uint64_t last=_NextListener;
    auto current=_Listeners.begin();
    while(current!=_Listeners.end()&&current->first<=last){
        const uint64_t id=current->first;
        try{current->second->OnRuntimeFrame(event);}catch(...){qWarning("Runtime frame listener threw an exception");}
        current=_Listeners.upper_bound(id);
    }
}
void RuntimeHost::NotifyPlayingChanged(){
    const uint64_t last=_NextListener;
    auto current=_Listeners.begin();
    while(current!=_Listeners.end()&&current->first<=last){
        const uint64_t id=current->first;
        try{current->second->OnSimulationPlayingChanged(_Playing);}catch(...){qWarning("Runtime state listener threw an exception");}
        current=_Listeners.upper_bound(id);
    }
}
void RuntimeHost::Release(GameSproutEditor::GameSproutModuleApi*){
    EndGameplay();
    _Listeners.clear();_FrameNumber=0;
    _Playing=false;_Timer.invalidate();
    _Gameplay=false;_Paused=false;_StepPending=false;_FixedAccumulator=0;
    if(_Runtime)_Runtime->SetResourceProvider(nullptr);
    _Runtime=nullptr;_Resources=nullptr;
}
