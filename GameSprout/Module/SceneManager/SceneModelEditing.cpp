#include "SceneManager.h"
#include <exception>

GameSproutRuntime::IGameObject* SceneManager::InstantiateModel(
    const GameSproutRuntime::ResourceReference& reference, uint64_t parentID, std::string& error) {
    if (!Ready(error)) return nullptr;
    auto* scene=GetActiveScene();
    auto* parent=parentID?_Runtime->QueryGameObject(parentID):scene;
    if (!parent||parent->GetScene()!=scene) {error="模型父节点已失效。";return nullptr;}
    _ExecutingEdit=true;
    struct Scope {bool& Flag;~Scope(){Flag=false;}} scope{_ExecutingEdit};
    GameSproutRuntime::IGameObject* object=nullptr;
    try {
        _History.reserve(_HistoryPosition+1);
        object=_Runtime->InstantiateModel(scene,reference,parent,error);
        if (!object) return nullptr;
        Edit edit;edit.Label="实例化模型";
        if (!CaptureDeletion(*object,edit,error)) {
            scene->DestroyGameObject(object);RefreshState(true);return nullptr;
        }
        edit.Type=Edit::Kind::Creation;
        _History.resize(_HistoryPosition);_History.push_back(std::move(edit));++_HistoryPosition;
        RefreshState(true);return object;
    } catch (const std::exception& exception) {error=exception.what();}
    catch (...) {error="模型实例化失败。";}
    if (object) scene->DestroyGameObject(object);
    RefreshState(true);return nullptr;
}
