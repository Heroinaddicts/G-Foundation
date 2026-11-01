#include "RuntimeHost.h"
#include "IRuntimeAssets.h"
#include <QDebug>

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
}
void RuntimeHost::ResetClock(){_Timer.start();}
void RuntimeHost::Update(GameSproutEditor::GameSproutModuleApi*){
    if(!_Runtime)return;
    const float elapsed=_Timer.isValid()?float(_Timer.nsecsElapsed()/1000000000.0):0.f;
    _Timer.start();
    _Runtime->Update(_Playing?elapsed:0.f);
}
void RuntimeHost::Release(GameSproutEditor::GameSproutModuleApi*){
    _Playing=false;_Timer.invalidate();
    if(_Runtime)_Runtime->SetResourceProvider(nullptr);
    _Runtime=nullptr;_Resources=nullptr;
}
