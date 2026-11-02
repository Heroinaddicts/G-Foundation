#include "PrefabManager.h"
#include "PrefabRecords.h"
#include "PrefabScriptFields.h"

bool PrefabManager::ConnectionAllowed(uint64_t objectID,const SceneSubtreeDocument& snapshot,std::string& error) const {
    try {
        Json::Value existing;std::string ignored;
        const auto rootFile=_Scenes->GetObjectFileID(objectID);
        if(FindRecord(objectID,existing,ignored)&&existing["RootFileID"].asUInt64()!=rootFile) {
            error="目标属于另一个Prefab实例的内部；Nested Prefab尚未开放。";return false;
        }
        std::set<uint64_t> nodes;for(const auto& identity:snapshot.ObjectIDs)if(!identity.Component)nodes.insert(identity.SceneFileID);
        const auto annotations=PrefabRecords::Parse(_Scenes->GetSceneAnnotationsJson());
        for(const auto& record:annotations["PrefabInstances"]) {
            const auto file=record["RootFileID"].asUInt64();
            if(file!=rootFile&&nodes.count(file)){error="目标子树包含已有Prefab实例；Nested Prefab尚未开放。";return false;}
        }
        return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}

void PrefabManager::RetryPending() {
    if(!_Scenes||!_Runtime||!_Project)return;
    auto* scene=_Scenes->GetActiveScene();const auto sceneID=scene?scene->GetInstanceID():0;
    std::set<std::string> ready;
    for(auto it=_Pending.begin();it!=_Pending.end();) {
        const auto& pending=it->second;auto* object=_Runtime->QueryGameObject(pending.ObjectID);
        if(pending.SceneEpoch!=_SceneEpoch||pending.SceneID!=sceneID||pending.ProjectRoot!=_Project->GetProjectRoot()||
           !object||object->GetScene()!=scene||_Scenes->GetObjectFileID(pending.ObjectID)!=pending.RootFileID)it=_Pending.erase(it);
        else {if(!pending.ReadyGuid.empty())ready.insert(pending.ReadyGuid);++it;}
    }
    if(_Busy||_Runtime->IsExecuting()||_Scenes->HasTransformEdit()||_Scenes->IsPlaySessionActive())return;
    for(const auto& guid:ready)
        QueueSource(AssetEvent(eAssetMetaEvent::Modify,Guid(guid),{},{},eAssetImporterType::Prefab,eAssetImportState::Ready,{}));
}

void PrefabManager::CompletePending(const std::string& guid) {
    if(!_Assets||!_Scenes||!_Runtime||!_Project||!_Library)return;
    const auto* meta=_Assets->QueryAsset(Guid(guid));
    if(!meta||meta->_ImporterType!=eAssetImporterType::Prefab||meta->GetImportState()!=eAssetImportState::Ready)return;
    const std::string path=meta->GetPath();auto found=_Pending.find(path);if(found==_Pending.end())return;
    found->second.ReadyGuid=guid;const auto pending=found->second;
    auto* scene=_Scenes->GetActiveScene();auto* object=_Runtime->QueryGameObject(pending.ObjectID);
    if(!scene||pending.SceneEpoch!=_SceneEpoch||pending.SceneID!=scene->GetInstanceID()||pending.ProjectRoot!=_Project->GetProjectRoot()||
       !object||object->GetScene()!=scene||_Scenes->GetObjectFileID(pending.ObjectID)!=pending.RootFileID) {_Pending.erase(path);return;}
    std::string error;if(!Ready(error))return;
    try {
        AssetImportManifest manifest;
        if(!_Library->GetManifest(Guid(guid),manifest)||manifest.SourcePath!=path)return;
        uint64_t mainID=0;
        for(const auto& asset:manifest.Assets)if(asset.Type==eAssetType::Prefab&&asset.SourceKey=="main") {
            if(mainID||!asset.LocalID)throw std::runtime_error("Prefab主资源身份无效。关闭连接请求，源文件仍保留。");
            mainID=asset.LocalID;
        }
        if(!mainID)return;
        const GameSproutRuntime::ResourceReference reference{guid,mainID};
        Json::Value latest;std::string sourcePath,hash;
        if(!Load(reference,latest,sourcePath,hash,true,error))throw std::runtime_error(error);
        Json::Value previous;std::string ignored;
        if(FindRecord(pending.ObjectID,previous,ignored)&&previous["RootFileID"].asUInt64()!=pending.RootFileID) {
            _Pending.erase(path);throw std::runtime_error("目标已经成为另一个Prefab实例的后代，已取消创建连接。");
        }
        SceneSubtreeDocument snapshot;if(!_Scenes->CaptureObjectDocument(pending.ObjectID,snapshot,error))throw std::runtime_error(error);
        if(!ConnectionAllowed(pending.ObjectID,snapshot,error)){_Pending.erase(path);throw std::runtime_error(error);}
        Json::Value record,raw;
        if(!AssetArtifactIO::Parse(pending.BaselineJson,record["Baseline"])||!AssetArtifactIO::Parse(pending.MappingsJson,record["Mappings"])||
           !AssetArtifactIO::Parse(snapshot.DocumentJson,raw))throw std::runtime_error("无法读取创建Prefab时保存的身份快照。");
        record["Version"]=1;record["Source"]["Guid"]=guid;record["Source"]["LocalID"]=Json::UInt64(mainID);
        record["RootFileID"]=Json::UInt64(pending.RootFileID);record["SourcePath"]=sourcePath;record["SourceHash"]=pending.SourceHash;
        record["Overrides"]=Json::Value(Json::arrayValue);
        auto current=PrefabRecords::Normalize(record,std::move(raw));
        const auto schemas=_Runtime->GetScriptTypes();
        if(!PrefabScriptFields::Align(record,current,nullptr,schemas,error))throw std::runtime_error(error);
        record["Overrides"]=PrefabRecords::Patches(PrefabRecords::EffectivePatches(record,current));
        SceneDocumentEditRequest request;request.Label="创建Prefab并连接";
        request.BuildAnnotations=[record](const SceneSubtreeDocument&,const std::string& previous,std::string& next,std::string& error) {
            try {auto annotations=PrefabRecords::Parse(previous);PrefabRecords::Put(annotations,record);next=AssetArtifactIO::JsonText(annotations);return true;}
            catch(const std::exception& exception){error=exception.what();return false;}
        };
        _Busy=true;struct Reset{bool& Flag;~Reset(){Flag=false;}} reset{_Busy};
        SceneSubtreeDocument result;
        if(!_Scenes->EditObjectDocument(request,result,error))throw std::runtime_error(error);
        _Pending.erase(path);_Errors.erase(pending.RootFileID);QueueNotify();
    }catch(const std::exception& exception) {
        if(_Console)_Console->ConsoleLog(__FILE__,__LINE__,eConsoleLog::Warning,"Prefab已保存，连接尚未完成："+std::string(exception.what()));
    }
}
