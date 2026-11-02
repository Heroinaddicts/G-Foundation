#ifndef __AssetOperations_h__
#define __AssetOperations_h__

#include "Header.h"
#include "IAssetsManager.h"
#include "ISceneManager.h"
class IAssetsManager;
class ISceneManager;
class IPrefabStage;
class IPlayMode;
class IRuntimeHost;
class ISelectionManager;
class QThread;

class AssetOperations : public IAssetOperations, public IAssetListener, public ISceneListener {
public:
    ~AssetOperations() override;
    bool PrepareDelete(const std::vector<std::string>&,AssetDeletePlan&,std::string&) const override;
    bool Delete(const AssetDeletePlan&,std::string&) override;
    void OnAssetCreate(const AssetEvent&) override;
    void OnAssetModify(const AssetEvent&) override;
    void OnAssetRemove(const AssetEvent&) override {}
    void OnActiveSceneChanged() override {RefreshProtectedSource();}
    void OnSceneStateChanged() override {RefreshProtectedSource();}

private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi * const api) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi * const api) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi * const api) override;
    void LaunchFinished(GameSproutEditor::GameSproutModuleApi * const api) override;
    void Update(GameSproutEditor::GameSproutModuleApi * const api) override;
    void Release(GameSproutEditor::GameSproutModuleApi * const api) override;
    bool Ready(std::string&) const;
    void RefreshProtectedSource() const;
    IAssetsManager* _Assets=nullptr;
    ISceneManager* _Scenes=nullptr;
    IPrefabStage* _Stage=nullptr;
    IPlayMode* _Play=nullptr;
    IRuntimeHost* _Host=nullptr;
    ISelectionManager* _Selection=nullptr;
    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr;
    QThread* _OwnerThread=nullptr;
    bool _Busy=false;
    bool _AssetSubscribed=false,_SceneSubscribed=false;
    mutable std::string _ProtectedSceneGuid,_ProtectedScenePath;
};

#endif //__AssetOperations_h__
