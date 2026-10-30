#include "SelectionManager.h"
#include "IAssetsPipeline.h"
#include <algorithm>
#include <QDebug>

bool SelectionManager::Initialize(GameSproutEditor::GameSproutModuleApi * api) {
    _Assets = api->_EditorApi->GetGameSproutModule<IAssetsManager>("AssetsManager");
    return _Assets != nullptr;
}
bool SelectionManager::Launch(GameSproutEditor::GameSproutModuleApi *) {
    _Subscribed = _Assets->RegisterAssetListener(this);
    return _Subscribed;
}
void SelectionManager::SetAssetSelection(const std::vector<ProjectAssetSelection> & selection) {
    std::vector<ProjectAssetSelection> valid;
    for (const auto & item : selection) {
        const Guid guid(item.GuidString);
        if (!guid.IsValid() || (item.IsSubAsset ? item.LocalID == 0 : item.LocalID != 0)) continue;
        ProjectAssetSelection normalized{guid.GetGuidString(), item.LocalID, item.IsSubAsset};
        if (std::find(valid.begin(), valid.end(), normalized) == valid.end()) valid.push_back(normalized);
    }
    if (_Selection == valid) return;
    _Selection = std::move(valid);
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
    std::vector<uint64_t> listeners;
    for (const auto & entry : _Listeners) listeners.push_back(entry.first);
    for (uint64_t id : listeners) {
        const auto found = _Listeners.find(id);
        if (found == _Listeners.end()) continue;
        try { found->second->OnAssetSelectionChanged(snapshot); }
        catch (...) { qWarning("Selection listener threw an exception"); }
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
void SelectionManager::Release(GameSproutEditor::GameSproutModuleApi *) {
    if (_Subscribed && _Assets) _Assets->UnregisterAssetListener(this);
    _Subscribed = false;
    _Assets = nullptr;
    _Listeners.clear();
    _Selection.clear();
    _Dirty = false;
}
