#include "ComponentEditContext.h"
#include "IObjectEditing.h"
GameSproutRuntime::IGameObject* ComponentEditContext::Resolve() const {
    if (!Runtime || !SceneID || !ObjectID) return nullptr;
    auto* object = Runtime->QueryGameObject(ObjectID);
    auto* scene = object ? object->GetScene() : nullptr;
    return scene && scene->GetInstanceID() == SceneID ? object : nullptr;
}
GameSproutRuntime::IComponent* ComponentEditContext::ResolveComponent() const {
    auto* object=Resolve();
    if(!object||!ComponentID)return nullptr;
    for(std::size_t index=0;index<object->GetComponentCount();++index) {
        auto* component=object->GetComponentAt(index);
        if(component&&component->GetComponentInstanceID()==ComponentID)return component;
    }
    return nullptr;
}
bool ComponentEditContext::Execute(const std::string& label, const ISceneManager::fSceneEditOperation& apply,
                                   const ISceneManager::fSceneEditOperation& revert) const {
    std::string error;
    // An explicit edit command validates the active document once. Display and
    // playback events resolve cached identities through Runtime only.
    auto* activeScene = Scenes ? Scenes->GetActiveScene() : nullptr;
    const bool targetValid=activeScene&&activeScene->GetInstanceID()==SceneID&&Resolve();
    const bool success=targetValid&&(Editing?Editing->EditObject({SceneID,ObjectID,ComponentID},label,apply,revert,error):
        !Scenes->IsPlaySessionActive()&&Scenes->EditObject(ObjectID,label,apply,revert,error));
    if (!success && error.empty()) error = "节点已经删除或场景已经切换。";
    if (Report) Report(error);
    return success;
}
std::string ComponentEditContext::Key(const std::string& component, const std::string& field) const {
    return std::to_string(SceneID) + "/" + std::to_string(ObjectID) + "/" + std::to_string(ComponentID) + "/" + component + "/" + field;
}
