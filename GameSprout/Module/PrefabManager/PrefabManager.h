#pragma once
#include "Header.h"
#include "ISceneManager.h"
#include "IAssetsManager.h"
#include "ILibraryManager.h"
#include "IProject.h"
#include "ISelectionManager.h"
#include "IConsole.h"
#include <QObject>
#include <map>
#include <set>
namespace Json {class Value;}

class PrefabManager final : public IPrefabManager,public ISceneListener,public IAssetListener {
public:
    ~PrefabManager() override;
    bool SavePrefab(uint64_t,const std::string&,std::string&) override;
    bool CreatePrefab(uint64_t,const std::string&,std::string&) override;
    bool CreatePrefabInDirectory(uint64_t,const std::string&,std::string&,std::string&) override;
    bool InstantiatePrefab(const GameSproutRuntime::ResourceReference&,uint64_t,uint64_t&,std::string&) override;
    bool ReadInstance(uint64_t,PrefabInstanceInfo&,std::string&) const override;
    bool ApplyOverrides(uint64_t,std::string&) override;
    bool RevertOverrides(uint64_t,std::string&) override;
    bool RefreshSource(const std::string&,std::string&) override;
    bool RegisterPrefabListener(IPrefabListener*) override;
    void UnregisterPrefabListener(IPrefabListener*) override;
    void OnActiveSceneChanged() override;
    void OnSceneStateChanged() override;
    void OnPlaySessionChanged(bool) override;
    void OnAssetCreate(const AssetEvent&) override;
    void OnAssetModify(const AssetEvent&) override;
    void OnAssetRemove(const AssetEvent&) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override;
    void LaunchFinished(GameSproutEditor::GameSproutModuleApi*) override;
    void Release(GameSproutEditor::GameSproutModuleApi*) override;
    bool Ready(std::string&) const;
    bool SaveSource(uint64_t,const std::string&,bool connect,std::string&);
    bool ConnectionAllowed(uint64_t,const SceneSubtreeDocument&,std::string&) const;
    void CompletePending(const std::string& guid);
    void RetryPending();
    bool Load(const GameSproutRuntime::ResourceReference&,Json::Value&,std::string&,std::string&,bool artifact,std::string&) const;
    bool FindRecord(uint64_t,Json::Value&,std::string&) const;
    bool Current(const Json::Value&,SceneSubtreeDocument&,Json::Value&,std::string&) const;
    bool Replace(const Json::Value&,const Json::Value&,bool clear,const std::string&,std::string&,const Json::Value* canonicalMap=nullptr,const std::vector<GameSproutRuntime::ScriptTypeInfo>* schemas=nullptr);
    void QueueSource(const AssetEvent&);
    void Notify();
    void QueueNotify();
    ISceneManager* _Scenes=nullptr;
    IAssetsManager* _Assets=nullptr;
    ILibraryManager* _Library=nullptr;
    IProject* _Project=nullptr;
    ISelectionManager* _Selection=nullptr;
    IConsole* _Console=nullptr;
    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr;
    QObject* _Dispatch=nullptr;
    bool _SceneSubscribed=false,_AssetSubscribed=false,_Busy=false,_Queued=false,_SourceQueued=false;
    std::set<std::string> _Sources;
    std::map<uint64_t,IPrefabListener*> _Listeners;
    std::map<uint64_t,std::string> _Errors;
    struct PendingCreation {
        uint64_t SceneID=0,ObjectID=0,RootFileID=0,SceneEpoch=0;
        std::string ProjectRoot,Path,BaselineJson,MappingsJson,SourceHash,ReadyGuid;
    };
    std::map<std::string,PendingCreation> _Pending;
    uint64_t _SceneEpoch=0;
    uint64_t _NextListener=0;
};
