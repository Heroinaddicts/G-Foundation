#include "ComponentEditContext.h"
GameSproutRuntime::IGameObject* ComponentEditContext::Resolve() const {
    if (!Runtime || !SceneID || !ObjectID) return nullptr;
    auto* object = Runtime->QueryGameObject(ObjectID);
    auto* scene = object ? object->GetScene() : nullptr;
    return scene && scene->GetInstanceID() == SceneID ? object : nullptr;
}
bool ComponentEditContext::Execute(const std::string& label, const ISceneManager::fSceneEditOperation& apply,
                                   const ISceneManager::fSceneEditOperation& revert) const {
    std::string error;
    // An explicit edit command validates the active document once. Display and
    // playback events resolve cached identities through Runtime only.
    auto* activeScene = Scenes ? Scenes->GetActiveScene() : nullptr;
    const bool success = activeScene && activeScene->GetInstanceID() == SceneID && Resolve()
        && Scenes->EditObject(ObjectID, label, apply, revert, error);
    if (!success && error.empty()) error = "节点已经删除或场景已经切换。";
    if (Report) Report(error);
    return success;
}
std::string ComponentEditContext::Key(const std::string& component, const std::string& field) const {
    return std::to_string(SceneID) + "/" + std::to_string(ObjectID) + "/" + component + "/" + field;
}
