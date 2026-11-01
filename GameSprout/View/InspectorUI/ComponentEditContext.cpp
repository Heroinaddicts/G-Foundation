#include "ComponentEditContext.h"
GameSproutRuntime::IGameObject* ComponentEditContext::Resolve() const {
    auto* scene = Scenes ? Scenes->GetActiveScene() : nullptr;
    auto* runtime = Runtime;
    if (!scene || !runtime || scene->GetInstanceID() != SceneID) return nullptr;
    auto* object = runtime->QueryGameObject(ObjectID);
    return object && object->GetScene() == scene ? object : nullptr;
}
bool ComponentEditContext::Execute(const std::string& label, const ISceneManager::fSceneEditOperation& apply,
                                   const ISceneManager::fSceneEditOperation& revert) const {
    std::string error;
    const bool success = Resolve() && Scenes->EditObject(ObjectID, label, apply, revert, error);
    if (!success && error.empty()) error = "节点已经删除或场景已经切换。";
    if (Report) Report(error);
    return success;
}
std::string ComponentEditContext::Key(const std::string& component, const std::string& field) const {
    return std::to_string(SceneID) + "/" + std::to_string(ObjectID) + "/" + component + "/" + field;
}
