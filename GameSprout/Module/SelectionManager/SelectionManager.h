#pragma once
#include "ISelectionManager.h"
#include "IAssetsManager.h"
#include "ISceneManager.h"
#include <map>

class SelectionManager final : public ISelectionManager, public IAssetListener,
    public ISceneListener, public GameSproutRuntime::IRuntimeListener {
public:
    std::vector<ProjectAssetSelection> GetAssetSelection() const override { return _Selection; }
    void SetAssetSelection(const std::vector<ProjectAssetSelection> & selection) override;
    SceneObjectSelection GetSceneObjectSelection() const override { return _SceneSelection; }
    void SetSceneObjectSelection(const SceneObjectSelection & selection) override;
    bool RegisterSelectionListener(ISelectionListener * listener) override;
    void UnregisterSelectionListener(ISelectionListener * listener) override;
    void OnAssetCreate(const AssetEvent &) override {}
    void OnAssetModify(const AssetEvent & event) override;
    void OnAssetRemove(const AssetEvent & event) override;
    void OnActiveSceneChanged() override;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent & event) override;
private:
    bool Initialize(GameSproutEditor::GameSproutModuleApi * api) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi *) override;
    void Update(GameSproutEditor::GameSproutModuleApi *) override;
    void Release(GameSproutEditor::GameSproutModuleApi *) override;
    IAssetsManager * _Assets = nullptr;
    ISceneManager * _Scenes = nullptr;
    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr; // Borrowed from EditorApi.
    bool _Subscribed = false;
    bool _SceneSubscribed = false;
    bool _RuntimeSubscribed = false;
    bool _Dirty = false;
    uint64_t _NextListener = 0;
    std::vector<ProjectAssetSelection> _Selection;
    SceneObjectSelection _SceneSelection;
    std::map<uint64_t, ISelectionListener *> _Listeners;
};
