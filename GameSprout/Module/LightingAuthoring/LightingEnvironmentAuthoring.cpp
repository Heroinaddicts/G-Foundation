#include "LightingAuthoring.h"
#include <QThread>
#include <QDebug>
#include <algorithm>
#include <limits>

namespace {
bool Same(const EnvironmentLightingSnapshot& a,const EnvironmentLightingSnapshot& b) {
    if(a.SceneID!=b.SceneID||a.Available!=b.Available||a.Editable!=b.Editable||
       a.Settings.Mode!=b.Settings.Mode||a.Settings.Intensity!=b.Settings.Intensity)return false;
    const auto color=[](GameSproutRuntime::Color x,GameSproutRuntime::Color y){return x.R==y.R&&x.G==y.G&&x.B==y.B&&x.A==y.A;};
    if(!color(a.Settings.AmbientColor,b.Settings.AmbientColor)||!color(a.Settings.SkyColor,b.Settings.SkyColor)||
       !color(a.Settings.EquatorColor,b.Settings.EquatorColor)||!color(a.Settings.GroundColor,b.Settings.GroundColor))return false;
    for(std::size_t index=0;index<a.Settings.SHCoefficients.size();++index) {
        const auto x=a.Settings.SHCoefficients[index],y=b.Settings.SHCoefficients[index];
        if(x.X!=y.X||x.Y!=y.Y||x.Z!=y.Z)return false;
    }
    return true;
}
}
bool LightingAuthoring::Launch(GameSproutEditor::GameSproutModuleApi*) {
    if(!_Scenes||!_Objects||!_Runtime||!_Play)return false;
    _SceneSubscribed=_Scenes->RegisterSceneListener(this);
    _PlaySubscribed=_Play->RegisterPlayModeListener(this);
    _RuntimeSubscribed=_Runtime->RegisterListener(this);
    return _SceneSubscribed&&_PlaySubscribed&&_RuntimeSubscribed;
}
bool LightingAuthoring::ReadEnvironment(EnvironmentLightingSnapshot& snapshot,std::string& error) const {
    error.clear();
    if(!_OwnerThread||QThread::currentThread()!=_OwnerThread||!_Scenes||!_Runtime||!_Play) {
        error="环境光照只能在所属主线程读取。";return false;
    }
    EnvironmentLightingSnapshot result;
    auto* scene=_Scenes->GetActiveScene();
    // Prefab Stage is an object document, not a source scene environment.
    if(scene&&!_Scenes->IsDocumentSessionActive()) {
        result.SceneID=scene->GetInstanceID();result.Available=true;
        result.Settings=scene->GetEnvironmentLighting();
        const auto state=_Play->GetState().State;
        result.Editable=(state==EditorPlayState::Edit&&!_Scenes->IsPlaySessionActive())||
            ((state==EditorPlayState::Playing||state==EditorPlayState::Paused)&&_Scenes->IsPlaySessionActive());
    }
    snapshot=result;return true;
}
bool LightingAuthoring::SetEnvironment(std::uint64_t sceneID,
    const GameSproutRuntime::EnvironmentLightingSettings& value,std::string& error) {
    EnvironmentLightingSnapshot snapshot;
    if(!ReadEnvironment(snapshot,error))return false;
    if(_NotifyingEnvironment||!snapshot.Available||!snapshot.Editable||snapshot.SceneID!=sceneID||!_Objects) {
        error="环境光照目标已切换、正在切换运行模式或处于 Prefab 编辑。";return false;
    }
    if(!GameSproutRuntime::IsValidEnvironmentLighting(value)) {
        error="环境光照参数无效；颜色、强度须为有限非负值，SH 系数须为有限值。";return false;
    }
    const auto operation=[](GameSproutRuntime::EnvironmentLightingSettings settings) {
        return [settings](GameSproutRuntime::IGameObject& object,std::string& failure) {
            auto* scene=dynamic_cast<GameSproutRuntime::IScene*>(&object);
            if(scene&&scene->SetEnvironmentLighting(settings))return true;
            failure="环境光照场景已经失效或参数无效。";return false;
        };
    };
    const bool accepted=_Objects->EditObject({sceneID,sceneID,0},"修改环境光照",
        operation(value),operation(snapshot.Settings),error);
    if(accepted)PublishEnvironment();
    return accepted;
}
bool LightingAuthoring::RegisterEnvironmentListener(ILightingEnvironmentListener* listener) {
    if(!_OwnerThread||QThread::currentThread()!=_OwnerThread||!listener||
       _NextEnvironmentListener==std::numeric_limits<std::uint64_t>::max())return false;
    for(const auto& entry:_EnvironmentListeners)if(entry.second==listener)return false;
    _EnvironmentListeners.emplace(++_NextEnvironmentListener,listener);return true;
}
void LightingAuthoring::UnregisterEnvironmentListener(ILightingEnvironmentListener* listener) {
    if(!_OwnerThread||QThread::currentThread()!=_OwnerThread)return;
    for(auto iterator=_EnvironmentListeners.begin();iterator!=_EnvironmentListeners.end();)
        if(iterator->second==listener)iterator=_EnvironmentListeners.erase(iterator);else ++iterator;
}
void LightingAuthoring::PublishEnvironment() {
    EnvironmentLightingSnapshot snapshot;std::string error;
    if(_NotifyingEnvironment||!ReadEnvironment(snapshot,error)||
       (_HaveEnvironmentSnapshot&&Same(snapshot,_LastEnvironmentSnapshot)))return;
    _LastEnvironmentSnapshot=snapshot;_HaveEnvironmentSnapshot=true;
    _NotifyingEnvironment=true;
    const auto last=_NextEnvironmentListener;
    auto iterator=_EnvironmentListeners.begin();
    while(iterator!=_EnvironmentListeners.end()&&iterator->first<=last) {
        const auto id=iterator->first;
        try {iterator->second->OnEnvironmentLightingChanged(snapshot);}
        catch(...) {qWarning("Environment lighting listener threw an exception");}
        iterator=_EnvironmentListeners.upper_bound(id);
    }
    _NotifyingEnvironment=false;
}
void LightingAuthoring::OnActiveSceneChanged(){PublishEnvironment();}
void LightingAuthoring::OnSceneStateChanged(){PublishEnvironment();}
void LightingAuthoring::OnPlayModeStateChanged(const PlayModeState&){PublishEnvironment();}
void LightingAuthoring::OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event) {
    if(event.Type!=GameSproutRuntime::SceneNodeEventType::EnvironmentLightingChanged)return;
    if(_Scenes&&_Scenes->GetActiveScene()&&event.SceneID==_Scenes->GetActiveScene()->GetInstanceID())PublishEnvironment();
}
