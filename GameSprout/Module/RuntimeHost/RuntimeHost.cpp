#include "RuntimeHost.h"
#include "IRuntimeAssets.h"
#include <QDebug>
#include <limits>

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
    if(_Playing==playing)return;
    _Playing=playing;ResetClock();
    NotifyPlayingChanged();
}
void RuntimeHost::ResetClock(){_Timer.start();}
void RuntimeHost::Update(GameSproutEditor::GameSproutModuleApi*){
    if(!_Runtime)return;
    const float elapsed=_Timer.isValid()?float(_Timer.nsecsElapsed()/1000000000.0):0.f;
    _Timer.start();
    const float delta=_Playing?elapsed:0.f;
    const bool playing=_Playing;
    _Runtime->Update(delta);
    NotifyFrame({++_FrameNumber,delta,playing});
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
    _Listeners.clear();_FrameNumber=0;
    _Playing=false;_Timer.invalidate();
    if(_Runtime)_Runtime->SetResourceProvider(nullptr);
    _Runtime=nullptr;_Resources=nullptr;
}
