#include "Scene.h"

namespace GameSproutRuntime {
IGameObject* Scene::CreateGameObject(const std::string& name, IGameObject* parent) {
    return State().Owner.CreateObject(this,name,parent ? parent : this);
}
bool Scene::DestroyGameObject(IGameObject* object) {
    RuntimeOperation operation(State().Owner);
    GameObjectState* node = State().Owner.Find(object);
    return node && node->SceneOwner == this && object != this && State().Owner.RequestDestroy(node,false);
}
bool Scene::SetMainCamera(ICameraComponent* camera) {
    std::uint64_t id=0;
    if(camera) {
        GameObjectState* node=State().Owner.Find(camera->GetGameObject());
        if(!State().Owner.IsUsable(node) || node->SceneOwner!=this ||
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
    return node && node->SceneOwner==this ? static_cast<ICameraComponent*>(node->GetComponent(typeid(ICameraComponent))) : nullptr;
}
void Scene::ClearMainCameraForNode(std::uint64_t id) {
    if(_MainCameraID==id) SetMainCamera(nullptr);
}
}
