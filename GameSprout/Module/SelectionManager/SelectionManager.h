#pragma once
#include "ISelectionManager.h"
#include "IAssetsManager.h"
#include <map>

class SelectionManager final : public ISelectionManager, public IAssetListener {
public:
    std::vector<ProjectAssetSelection> GetAssetSelection() const override { return _Selection; }
    void SetAssetSelection(const std::vector<ProjectAssetSelection> & selection) override;
    bool RegisterSelectionListener(ISelectionListener * listener) override;
    void UnregisterSelectionListener(ISelectionListener * listener) override;
    void OnAssetCreate(const AssetEvent &) override {}
    void OnAssetModify(const AssetEvent & event) override;
    void OnAssetRemove(const AssetEvent & event) override;
private:
    bool Initialize(GameSprout::GameSproutApi * api) override;
    bool Launch(GameSprout::GameSproutApi *) override;
    void Update(GameSprout::GameSproutApi *) override;
    void Release(GameSprout::GameSproutApi *) override;
    IAssetsManager * _Assets = nullptr;
    bool _Subscribed = false;
    bool _Dirty = false;
    uint64_t _NextListener = 0;
    std::vector<ProjectAssetSelection> _Selection;
    std::map<uint64_t, ISelectionListener *> _Listeners;
};
