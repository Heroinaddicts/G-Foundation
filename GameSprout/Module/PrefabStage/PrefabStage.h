#pragma once
#include "Header.h"
#include "IAssetsManager.h"
#include "ISceneManager.h"
#include "ISelectionManager.h"
#include <QObject>
#include <map>
class IProject;
class IPrefabManager;
class IPlayMode;
class IAnimationPreview;
class IRuntimeHost;
namespace Json {class Value;}

class PrefabStage final : public IPrefabStage,public ISceneListener,public IAssetListener {
public:
    ~PrefabStage() override;
    PrefabStageState GetState() const override {return _State;}
    bool OpenPrefab(const std::string&,std::string&) override;
    bool Save(std::string&) override;
    bool Close(bool,std::string&) override;
    bool RegisterStageListener(IPrefabStageListener*) override;
    void UnregisterStageListener(IPrefabStageListener*) override;
    void OnActiveSceneChanged() override;
    void OnSceneStateChanged() override;
    void OnAssetCreate(const AssetEvent&) override;
    void OnAssetModify(const AssetEvent&) override;
    void OnAssetRemove(const AssetEvent&) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override;
    void Update(GameSproutEditor::GameSproutModuleApi*) override {}
    void Release(GameSproutEditor::GameSproutModuleApi*) override;
    bool Ready(std::string&) const;
    bool ReadSource(const std::string&,Json::Value&,std::string&,std::string&,std::string&) const;
    bool Capture(Json::Value&,std::string&,std::string&) const;
    bool WriteSource(const Json::Value&,const std::string&,std::string&,std::string&);
    void ObserveSource();
    void QueueObserve(const AssetEvent&);
    void Notify();
    void RestoreSelection();
    ISceneManager* _Scenes=nullptr;
    IAssetsManager* _Assets=nullptr;
    IProject* _Project=nullptr;
    IPrefabManager* _Prefabs=nullptr;
    ISelectionManager* _Selection=nullptr;
    IPlayMode* _Play=nullptr;
    IAnimationPreview* _Preview=nullptr;
    IRuntimeHost* _Host=nullptr;
    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr;
    QObject* _Dispatch=nullptr;
    PrefabStageState _State;
    std::string _DiskHash,_SavedText;
    SceneObjectSelection _PreviousSceneSelection;
    std::vector<ProjectAssetSelection> _PreviousAssets;
    std::map<uint64_t,IPrefabStageListener*> _Listeners;
    uint64_t _NextListener=0;
    bool _SceneSubscribed=false,_AssetSubscribed=false,_Busy=false,_Observing=false,_Notifying=false;
};
