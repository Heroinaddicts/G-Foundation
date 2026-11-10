#include "Scene.h"

namespace GameSproutRuntime {
void Scene::InvalidateRenderSnapshot(std::uint64_t transformObjectID) {
    ++_RenderRevision;
    _VisualPending=true;
    if(!transformObjectID) {
        _RenderTransformChanges.clear();
        return;
    }
    try {
        _RenderTransformChanges.push_back(transformObjectID);
        if(_RenderTransformChanges.size()>2048)_RenderTransformChanges.pop_front();
    } catch(...) {_RenderTransformChanges.clear();}
}
bool Scene::GetRenderTransformChanges(std::uint64_t after,std::vector<std::uint64_t>& changes) const {
    if(after>_RenderRevision||_RenderRevision-after>_RenderTransformChanges.size())return false;
    const auto count=static_cast<std::ptrdiff_t>(_RenderRevision-after);
    changes.assign(_RenderTransformChanges.end()-count,_RenderTransformChanges.end());
    return true;
}
bool Scene::SetEnvironmentLighting(const EnvironmentLightingSettings& settings) {
    if(!IsValidEnvironmentLighting(settings))return false;
    const auto sameColor=[](Color a,Color b){return a.R==b.R&&a.G==b.G&&a.B==b.B&&a.A==b.A;};
    bool same=settings.Mode==_EnvironmentLighting.Mode&&settings.Intensity==_EnvironmentLighting.Intensity&&
        settings.Atmosphere==_EnvironmentLighting.Atmosphere&&
        sameColor(settings.AmbientColor,_EnvironmentLighting.AmbientColor)&&sameColor(settings.SkyColor,_EnvironmentLighting.SkyColor)&&
        sameColor(settings.EquatorColor,_EnvironmentLighting.EquatorColor)&&sameColor(settings.GroundColor,_EnvironmentLighting.GroundColor);
    for(std::size_t index=0;same&&index<settings.SHCoefficients.size();++index) {
        const auto a=settings.SHCoefficients[index],b=_EnvironmentLighting.SHCoefficients[index];
        same=a.X==b.X&&a.Y==b.Y&&a.Z==b.Z;
    }
    if(same)return true;
    _EnvironmentLighting=settings;
    State().Owner.Notify(State(),SceneNodeEventType::EnvironmentLightingChanged);
    return true;
}
IGameObject* Scene::CreateGameObject(const std::string& name, IGameObject* parent) {
    return State().Owner.CreateObject(this,name,parent ? parent : this);
}
bool Scene::DestroyGameObject(IGameObject* object) {
    RuntimeOperation operation(State().Owner);
    GameObjectState* node = State().Owner.Find(object);
    return node && node->SceneOwner == this && object != this && State().Owner.RequestDestroy(node,false);
}
void Scene::SetSimulationEnabled(bool enabled) { _SimulationEnabled=enabled; }
bool Scene::IsSimulationEnabled() const { return _SimulationEnabled; }
bool Scene::SetMainCamera(ICameraComponent* camera) {
    std::uint64_t id=0;
    if(camera) {
        GameObjectState* node=State().Owner.Find(camera->GetGameObject());
            if(!State().Owner.IsInScene(node) || node->SceneOwner!=this ||
           node->GetComponent(typeid(ICameraComponent))!=camera) return false;
        id=node->ID;
    }
    if(_MainCameraID==id) return true;
    _MainCameraID=id;
    State().Owner.Notify(State(),SceneNodeEventType::MainCameraChanged);
    return true;
}
ICameraComponent* Scene::GetMainCamera() const {
    GameObjectState* node=State().Owner.Find(_MainCameraID);
    return State().Owner.IsInScene(node) && node->SceneOwner==this ? static_cast<ICameraComponent*>(node->GetComponent(typeid(ICameraComponent))) : nullptr;
}
void Scene::ClearMainCameraForNode(std::uint64_t id) {
    if(_MainCameraID==id) SetMainCamera(nullptr);
}
}
