#include "PrefabManager.h"
#include "PrefabRecords.h"
#include "PrefabScriptFields.h"
#include <algorithm>

bool PrefabManager::FindRecord(uint64_t objectID,Json::Value& record,std::string& error) const {
    try {
        if(!_Scenes||!_Runtime||!objectID){error="没有选中的场景对象。";return false;}
        auto* object=_Runtime->QueryGameObject(objectID);
        if(!object||object->GetScene()!=_Scenes->GetActiveScene()){error="选中的对象已失效。";return false;}
        std::set<uint64_t> ancestors;
        for(auto* node=object;node&&node!=_Scenes->GetActiveScene();node=node->Parent())
            if(const auto file=_Scenes->GetObjectFileID(node->GetInstanceID()))ancestors.insert(file);
        const auto annotations=PrefabRecords::Parse(_Scenes->GetSceneAnnotationsJson());
        for(const auto& item:annotations["PrefabInstances"]) {
            if(item["Version"]!=1||!item["Baseline"].isObject()||!item["Source"].isObject())throw std::runtime_error("Invalid current Prefab instance record");
            const auto mapping=PrefabRecords::Mapping(item,true);
            for(const auto file:ancestors)if(mapping.count(file)){record=item;error.clear();return true;}
        }
        error="当前对象不属于Prefab实例。";return false;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
bool PrefabManager::Current(const Json::Value& record,SceneSubtreeDocument& snapshot,Json::Value& normalized,std::string& error) const {
    auto* root=_Scenes->QueryObjectByFileID(record["RootFileID"].asUInt64());
    if(!root){error="Prefab实例根已删除。";return false;}
    if(!_Scenes->CaptureObjectDocument(root->GetInstanceID(),snapshot,error))return false;
    Json::Value current;
    if(!AssetArtifactIO::Parse(snapshot.DocumentJson,current)){error="无法解析实例快照。";return false;}
    try{normalized=PrefabRecords::Normalize(record,std::move(current));return true;}
    catch(const std::exception& exception){error=exception.what();return false;}
}
bool PrefabManager::ReadInstance(uint64_t objectID,PrefabInstanceInfo& info,std::string& error) const {
    info={};Json::Value record;if(!FindRecord(objectID,record,error))return false;
    try {
        SceneSubtreeDocument snapshot;Json::Value current;if(!Current(record,snapshot,current,error))return false;
        const auto schemas=_Runtime->GetScriptTypes();
        if(!PrefabScriptFields::Align(record,current,nullptr,schemas,error))return false;
        info.RootObjectID=snapshot.RootObjectID;info.RootFileID=record["RootFileID"].asUInt64();
        info.Source={record["Source"]["Guid"].asString(),record["Source"]["LocalID"].asUInt64()};info.SourcePath=record["SourcePath"].asString();
        Json::Value source;std::string hash,path,sourceError;
        info.MissingSource=!Load(info.Source,source,path,hash,false,sourceError);
        if(info.MissingSource)info.Error=sourceError;else info.SourcePath=path;
        if(const auto found=_Errors.find(info.RootFileID);found!=_Errors.end())info.Error=found->second;
        const auto currentNodes=PrefabDefinition::Objects(current["Objects"]),baselineNodes=PrefabDefinition::Objects(record["Baseline"]["Objects"]);
        for(const auto& patch:PrefabRecords::EffectivePatches(record,current)) {
            PrefabOverrideInfo item;item.SourceObjectID=patch.ObjectID;item.ComponentID=patch.ComponentID;
            const Json::Value* owner=nullptr;
            if(currentNodes.count(patch.ObjectID))owner=&currentNodes.at(patch.ObjectID);
            else if(baselineNodes.count(patch.ObjectID))owner=&baselineNodes.at(patch.ObjectID);
            else if(patch.Path==std::vector<std::string>{"$Object"})owner=patch.Remove?&patch.Before:&patch.After;
            if(owner)item.ObjectName=(*owner)["Name"].asString();
            if(patch.ComponentID) {
                const Json::Value* component=nullptr;
                if(owner)for(const auto& value:(*owner)["Components"])if(value["ID"].asUInt64()==patch.ComponentID){component=&value;break;}
                if(!component&&baselineNodes.count(patch.ObjectID))for(const auto& value:baselineNodes.at(patch.ObjectID)["Components"])
                    if(value["ID"].asUInt64()==patch.ComponentID){component=&value;break;}
                if(!component&&patch.Path==std::vector<std::string>{"$Component"})component=patch.Remove?&patch.Before:&patch.After;
                if(component)item.ComponentType=(*component)["Type"].asString();
            }
            for(const auto& part:patch.Path)item.PropertyPath+="/"+part;
            item.Kind=patch.Path==std::vector<std::string>{"$Object"}?(patch.Remove?PrefabOverrideKind::RemovedObject:PrefabOverrideKind::AddedObject):
                patch.Path==std::vector<std::string>{"$Component"}?(patch.Remove?PrefabOverrideKind::RemovedComponent:PrefabOverrideKind::AddedComponent):PrefabOverrideKind::Property;
            item.SourceValueJson=AssetArtifactIO::JsonText(patch.Before);item.InstanceValueJson=AssetArtifactIO::JsonText(patch.After);info.Overrides.push_back(std::move(item));
        }
        info.HasOverrides=!info.Overrides.empty();error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
bool PrefabManager::InstantiatePrefab(const GameSproutRuntime::ResourceReference& reference,uint64_t parentID,uint64_t& objectID,std::string& error) {
    if(!Ready(error))return false;
    Json::Value data;std::string path,hash;if(!Load(reference,data,path,hash,true,error))return false;
    const auto schemas=_Runtime->GetScriptTypes();
    if(!SceneScriptDocument::AlignFields(data,schemas,error))return false;
    if(parentID) {Json::Value connected;std::string ignored;if(FindRecord(parentID,connected,ignored)){error="Nested Prefab预留，第一版不能向Prefab实例内插入另一个Prefab。";return false;}}
    _Busy=true;struct Reset{bool& value;~Reset(){value=false;}} reset{_Busy};
    SceneDocumentEditRequest request;request.ParentObjectID=parentID;request.DocumentJson=AssetArtifactIO::JsonText(data);
    request.RemapDocumentIDs=true;request.Label="实例化Prefab";
    const auto sourceRoot=PrefabDefinition::RootID(data);
    request.BuildAnnotations=[reference,path,hash,data,sourceRoot](const SceneSubtreeDocument& result,const std::string& previous,std::string& next,std::string& message) {
        try {
            auto annotations=PrefabRecords::Parse(previous);Json::Value record;
            record["Version"]=1;record["Source"]["Guid"]=reference.Guid;record["Source"]["LocalID"]=Json::UInt64(reference.LocalID);
            record["SourcePath"]=path;record["SourceHash"]=hash;record["Baseline"]=data;record["Mappings"]=PrefabRecords::Mappings(result);
            record["Overrides"]=Json::Value(Json::arrayValue);
            uint64_t rootFile=0;for(const auto& mapping:result.ObjectIDs)if(!mapping.Component&&mapping.DocumentFileID==sourceRoot)rootFile=mapping.SceneFileID;
            if(!rootFile)throw std::runtime_error("Prefab root identity mapping is missing");
            record["RootFileID"]=Json::UInt64(rootFile);PrefabRecords::Put(annotations,record);next=AssetArtifactIO::JsonText(annotations);return true;
        }catch(const std::exception& exception){message=exception.what();return false;}
    };
    SceneSubtreeDocument result;if(!_Scenes->EditObjectDocument(request,result,error))return false;
    objectID=result.RootObjectID;QueueNotify();return true;
}
bool PrefabManager::Replace(const Json::Value& original,const Json::Value& source,bool clear,const std::string& hash,std::string& error,const Json::Value* canonicalMap,const std::vector<GameSproutRuntime::ScriptTypeInfo>* providedSchemas) {
    SceneSubtreeDocument snapshot;Json::Value current;if(!Current(original,snapshot,current,error))return false;
    try {
        Json::Value record=original,alignedSource=source;
        std::vector<GameSproutRuntime::ScriptTypeInfo> ownedSchemas;
        if(!providedSchemas)ownedSchemas=_Runtime->GetScriptTypes();
        const auto& schemas=providedSchemas?*providedSchemas:ownedSchemas;
        if(!PrefabScriptFields::Align(record,current,&alignedSource,schemas,error))return false;
        if(canonicalMap) {
            std::map<uint64_t,uint64_t> ids;
            for(const auto& item:*canonicalMap)ids[item["Old"].asUInt64()]=item["New"].asUInt64();
            // Added instance objects are not in the old source mapping yet.
            // Applying promotes them to source identities, while their existing
            // scene FileIDs/Runtime objects must remain the same.
            auto known=PrefabRecords::Mapping(record);
            for(const auto& node:current["Objects"]) {
                const auto remember=[&](const Json::Value& value,bool component) {
                    const auto old=value["ID"].asUInt64();
                    if((old&PrefabDefinition::InstanceBit)&&!known.count(old)) {
                        Json::Value mapping;mapping["SourceID"]=Json::UInt64(old);mapping["SceneID"]=Json::UInt64(old&~PrefabDefinition::InstanceBit);
                        mapping["Component"]=component;record["Mappings"].append(mapping);known[old]=old&~PrefabDefinition::InstanceBit;
                    }
                };
                remember(node,false);for(const auto& component:node["Components"])remember(component,true);
            }
            PrefabDefinition::RemapReferences(current,ids);
            for(auto& item:record["Mappings"])if(ids.count(item["SourceID"].asUInt64()))item["SourceID"]=Json::UInt64(ids.at(item["SourceID"].asUInt64()));
        }
        const auto patches=clear?std::vector<PrefabDefinition::Patch>{}:PrefabRecords::EffectivePatches(record,current);
        const auto combined=PrefabDefinition::Merge(record["Baseline"],current,alignedSource,&patches);
        Json::Value raw;if(!AssetArtifactIO::Parse(snapshot.DocumentJson,raw))throw std::runtime_error("Invalid scene subtree snapshot");
        Json::Value mappings;auto target=PrefabRecords::Materialize(record,combined,raw["NextObjectID"].asUInt64(),mappings);
        Json::Value newRecord=record;newRecord["Baseline"]=alignedSource;newRecord["SourceHash"]=hash;newRecord["Mappings"]=std::move(mappings);newRecord["Overrides"]=PrefabRecords::Patches(patches);
        SceneDocumentEditRequest request;request.RootObjectID=snapshot.RootObjectID;
        request.Label=clear?"还原Prefab覆盖":"更新Prefab来源";request.DocumentJson=AssetArtifactIO::JsonText(target);
        request.BuildAnnotations=[newRecord](const SceneSubtreeDocument&,const std::string& previous,std::string& next,std::string& message){
            try{auto annotations=PrefabRecords::Parse(previous);PrefabRecords::Put(annotations,newRecord);next=AssetArtifactIO::JsonText(annotations);return true;}
            catch(const std::exception& exception){message=exception.what();return false;}
        };
        SceneSubtreeDocument result;
        if(!_Scenes->EditObjectDocument(request,result,error))return false;
        _Errors.erase(record["RootFileID"].asUInt64());QueueNotify();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
bool PrefabManager::RevertOverrides(uint64_t objectID,std::string& error) {
    if(!Ready(error))return false;Json::Value record;if(!FindRecord(objectID,record,error))return false;
    Json::Value source;std::string path,hash;
    if(!Load({record["Source"]["Guid"].asString(),record["Source"]["LocalID"].asUInt64()},source,path,hash,false,error))return false;
    _Busy=true;struct Reset{bool& value;~Reset(){value=false;}} reset{_Busy};
    return Replace(record,source,true,hash,error);
}
bool PrefabManager::RefreshSource(const std::string& guid,std::string& error) {
    if(!Ready(error))return false;
    try {
        const auto annotations=PrefabRecords::Parse(_Scenes->GetSceneAnnotationsJson());bool success=true;
        const auto schemas=_Runtime->GetScriptTypes();
        _Busy=true;struct Reset{bool& value;~Reset(){value=false;}} reset{_Busy};
        for(const auto& record:annotations["PrefabInstances"]) {
            if(record["Source"]["Guid"]!=guid||!_Scenes->QueryObjectByFileID(record["RootFileID"].asUInt64()))continue;
            Json::Value source;std::string path,hash,message;
            if(!Load({guid,record["Source"]["LocalID"].asUInt64()},source,path,hash,false,message)||
               (record["SourceHash"]!=hash&&!Replace(record,source,false,hash,message,nullptr,&schemas))) {
                _Errors[record["RootFileID"].asUInt64()]=message;error+=message+"\n";success=false;
            } else _Errors.erase(record["RootFileID"].asUInt64());
        }
        QueueNotify();return success;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
