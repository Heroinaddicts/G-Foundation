#include "SelectionManager.h"
#include "IAssetsPipeline.h"
#include <algorithm>
#include <QDebug>

bool SelectionManager::Initialize(GameSproutEditor::GameSproutModuleApi * api) {
    _Assets = api->_EditorApi->GetGameSproutModule<IAssetsManager>("AssetsManager");
    _Scenes = api->_EditorApi->GetGameSproutModule<ISceneManager>("SceneManager");
    return _Assets != nullptr;
}
bool SelectionManager::Launch(GameSproutEditor::GameSproutModuleApi *) {
    _Subscribed = _Assets->RegisterAssetListener(this);
    if (!_Subscribed) return false;
    if (_Scenes) {
        _SceneSubscribed = _Scenes->RegisterSceneListener(this);
        _Runtime = _Scenes->GetRuntime();
        _RuntimeSubscribed = _Runtime && _Runtime->RegisterListener(this);
        if (!_SceneSubscribed || !_RuntimeSubscribed) return false;
    }
    return true;
}
void SelectionManager::SetAssetSelection(const std::vector<ProjectAssetSelection> & selection) {
    std::vector<ProjectAssetSelection> valid;
    for (const auto & item : selection) {
        const Guid guid(item.GuidString);
        if (!guid.IsValid() || (item.IsSubAsset ? item.LocalID == 0 : item.LocalID != 0)) continue;
        ProjectAssetSelection normalized{guid.GetGuidString(), item.LocalID, item.IsSubAsset};
        if (std::find(valid.begin(), valid.end(), normalized) == valid.end()) valid.push_back(normalized);
    }
    if (_Selection == valid && (valid.empty() || _SceneSelection.ObjectID == 0)) return;
    if (!valid.empty()) _SceneSelection = {};
    _Selection = std::move(valid);
    _Dirty = true;
}
void SelectionManager::SetSceneObjectSelection(const SceneObjectSelection & selection) {
    SceneObjectSelection valid;
    GameSproutRuntime::IScene * scene = _Scenes ? _Scenes->GetActiveScene() : nullptr;
    GameSproutRuntime::IGameSproutRuntime * runtime = _Scenes ? _Scenes->GetRuntime() : nullptr;
    if (scene && runtime && scene->GetInstanceID() == selection.SceneID && selection.ObjectID) {
        GameSproutRuntime::IGameObject * object = runtime->QueryGameObject(selection.ObjectID);
        if (object && object->GetScene() == scene) valid = selection;
    }
    if (_SceneSelection == valid && (!valid.ObjectID || _Selection.empty())) return;
    _SceneSelection = valid;
    if (valid.ObjectID) _Selection.clear();
    _Dirty = true;
}
bool SelectionManager::RegisterSelectionListener(ISelectionListener * listener) {
    if (!listener) return false;
    for (const auto & entry : _Listeners) if (entry.second == listener) return false;
    _Listeners.emplace(++_NextListener, listener);
    return true;
}
void SelectionManager::UnregisterSelectionListener(ISelectionListener * listener) {
    for (auto it = _Listeners.begin(); it != _Listeners.end();) {
        if (it->second == listener) it = _Listeners.erase(it); else ++it;
    }
}
void SelectionManager::Update(GameSproutEditor::GameSproutModuleApi *) {
    if (!_Dirty) return;
    _Dirty = false;
    const auto snapshot = _Selection;
    const SceneObjectSelection sceneSnapshot = _SceneSelection;
    std::vector<uint64_t> listeners;
    for (const auto & entry : _Listeners) listeners.push_back(entry.first);
    for (uint64_t id : listeners) {
        const auto found = _Listeners.find(id);
        if (found == _Listeners.end()) continue;
        try { found->second->OnAssetSelectionChanged(snapshot); }
        catch (...) { qWarning("Selection listener threw an exception"); }
        // 第一个回调允许注销自己，因此第二个回调前重新查询登记。
        const auto current = _Listeners.find(id);
        if (current == _Listeners.end()) continue;
        try { current->second->OnSceneObjectSelectionChanged(sceneSnapshot); }
        catch (...) { qWarning("Scene selection listener threw an exception"); }
    }
}
void SelectionManager::OnAssetModify(const AssetEvent & event) {
    auto selection = _Selection;
    IAssetMeta * meta = _Assets->QueryAsset(event.GuidValue);
    if (!meta) return;
    const auto children = meta->GetSubAssets();
    std::erase_if(selection, [&](const ProjectAssetSelection & item) {
        return item.GuidString == event.GuidValue.GetGuidString() && item.IsSubAsset
            && std::none_of(children.begin(), children.end(), [&](IAsset * child) { return child->GetLocalID() == item.LocalID; });
    });
    SetAssetSelection(selection);
}
void SelectionManager::OnAssetRemove(const AssetEvent & event) {
    auto selection = _Selection;
    std::erase_if(selection, [&](const ProjectAssetSelection & item) { return item.GuidString == event.GuidValue.GetGuidString(); });
    SetAssetSelection(selection);
}
void SelectionManager::OnActiveSceneChanged() {
    SetSceneObjectSelection({});
}
void SelectionManager::OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent & event) {
    if (event.Type == GameSproutRuntime::SceneNodeEventType::Destroy &&
        (event.ObjectID == _SceneSelection.ObjectID || event.ObjectID == _SceneSelection.SceneID))
        SetSceneObjectSelection({});
}
void SelectionManager::Release(GameSproutEditor::GameSproutModuleApi *) {
    if (_Subscribed && _Assets) _Assets->UnregisterAssetListener(this);
    if (_SceneSubscribed && _Scenes) _Scenes->UnregisterSceneListener(this);
    // Editor 先释放全部 View。模块中 SceneManager 可能先释放，需通过服务确认 Runtime 仍存活。
    if (_RuntimeSubscribed && _Scenes && _Scenes->GetRuntime() == _Runtime && _Runtime)
        _Runtime->UnregisterListener(this);
    _Subscribed = false;
    _SceneSubscribed = false;
    _RuntimeSubscribed = false;
    _Assets = nullptr;
    _Scenes = nullptr;
    _Runtime = nullptr;
    _Listeners.clear();
    _Selection.clear();
    _SceneSelection = {};
    _Dirty = false;
}
