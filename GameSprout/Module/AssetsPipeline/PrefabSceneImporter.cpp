#include "AssetImportJob.h"
#include "../Shared/PrefabInstanceDocument.h"
#include <algorithm>
#include <limits>
#include <set>

namespace {
std::uint64_t NextIdentity(const Json::Value& scene) {
    if(scene.isMember("NextObjectID"))return scene["NextObjectID"].asUInt64();
    std::uint64_t maximum=0;
    const auto components=[&](const Json::Value& values) {
        for(const auto& value:values)if(value.isMember("ID"))maximum=std::max(maximum,value["ID"].asUInt64());
    };
    components(scene["RootComponents"]);
    for(const auto& node:scene["Objects"]) {maximum=std::max(maximum,node["ID"].asUInt64());components(node["Components"]);}
    return maximum==std::numeric_limits<std::uint64_t>::max()?0:maximum+1;
}
Json::Value Capture(const Json::Value& scene,std::uint64_t root,std::set<std::uint64_t>& selected) {
    std::map<std::uint64_t,std::vector<std::uint64_t>> children;
    for(const auto& node:scene["Objects"])children[node["ParentID"].asUInt64()].push_back(node["ID"].asUInt64());
    std::vector<std::uint64_t> pending{root};
    while(!pending.empty()) {
        const auto id=pending.back();pending.pop_back();
        if(!selected.insert(id).second)throw std::runtime_error("Prefab instance hierarchy contains a cycle");
        if(const auto found=children.find(id);found!=children.end())pending.insert(pending.end(),found->second.begin(),found->second.end());
    }
    Json::Value result(Json::objectValue);result["Version"]=1;result["Name"]="Prefab instance";
    result["NextObjectID"]=Json::UInt64(NextIdentity(scene));
    const auto camera=scene.get("MainCameraID",Json::UInt64(0)).asUInt64();
    result["MainCameraID"]=Json::UInt64(selected.count(camera)?camera:0);
    result["Objects"]=Json::Value(Json::arrayValue);
    for(auto node:scene["Objects"])if(selected.count(node["ID"].asUInt64())) {
        if(node["ID"].asUInt64()==root)node["ParentID"]=Json::UInt64(0);
        result["Objects"].append(std::move(node));
    }
    return result;
}
void CheckRecord(const Json::Value& record) {
    if(!record.isObject()||record["Version"]!=1||!record["RootFileID"].isUInt64()||!record["RootFileID"].asUInt64()||
       !record["Source"].isObject()||!record["Source"]["Guid"].isString()||!Guid(record["Source"]["Guid"].asString()).IsValid()||
       !record["Source"]["LocalID"].isUInt64()||!record["Source"]["LocalID"].asUInt64()||!record["Baseline"].isObject()||
       !record["Overrides"].isArray())throw std::runtime_error("Invalid current Prefab instance record in scene");
    std::string error;
    Json::Value ignored;
    if(!PrefabDefinition::Validate(PrefabDefinition::Source(record["Baseline"]),ignored,error))
        throw std::runtime_error("Invalid Prefab instance baseline: "+error);
    PrefabRecords::Mapping(record);PrefabRecords::Mapping(record,true);
}
}

bool ResolveScenePrefabInstances(AssetImportJob& job,Json::Value& document) {
    try {
        std::string error;
        if(!SceneDocument::Validate(document,error))return job.Fail("Invalid scene document: "+error);
        Json::Value resolved=document;
        const auto& annotations=document["Annotations"];
        if(annotations.isMember("PrefabInstances")&&!annotations["PrefabInstances"].isArray())
            return job.Fail("Scene PrefabInstances annotations must be an array");
        std::set<std::uint64_t> resolvedNodes;
        for(const auto& record:annotations["PrefabInstances"]) {
            if(job.Cancelled())return false;
            CheckRecord(record);
            const auto root=record["RootFileID"].asUInt64();
            const auto nodes=PrefabDefinition::Objects(resolved["Objects"]);
            // Deleting an instance can leave its authoring record until the next
            // editor reconciliation. It must never resurrect the deleted root.
            if(!nodes.count(root))continue;
            std::set<std::uint64_t> selected;
            const auto current=Capture(resolved,root,selected);
            for(const auto id:selected)if(resolvedNodes.count(id))return job.Fail("Nested or overlapping scene Prefab instances are reserved");
            resolvedNodes.insert(selected.begin(),selected.end());
            const auto sourceGuid=record["Source"]["Guid"].asString();
            const std::string sourcePath=job.TrackGuidReference(sourceGuid);
            if(sourcePath.empty()) {
                // A disconnected instance still has its materialized scene
                // nodes and overrides. Missing sources must not erase them.
                job.Result.Diagnostics+="Missing Prefab source retained with scene instance: "+sourceGuid+"\n";
                continue;
            }
            if(QFileInfo(QString::fromStdString(sourcePath)).suffix().compare("prefab",Qt::CaseInsensitive)!=0)
                return job.Fail("Prefab scene source GUID does not identify a .prefab source");
            QByteArray bytes;
            if(!job.AddDependency(sourcePath,&bytes))return false;
            if(bytes.size()>64*1024*1024)return job.Fail("Prefab scene source exceeds 64 MiB");
            Json::Value source,data;
            if(!AssetArtifactIO::Parse(bytes.toStdString(),source)||!PrefabDefinition::Validate(source,data,error))
                return job.Fail("Invalid Prefab scene source "+sourcePath+": "+error);
            const auto normalized=PrefabRecords::Normalize(record,current);
            const auto patches=PrefabRecords::EffectivePatches(record,normalized);
            const auto merged=PrefabDefinition::Merge(record["Baseline"],normalized,data,&patches);
            Json::Value mappings;
            auto materialized=PrefabRecords::Materialize(record,merged,NextIdentity(resolved),mappings);
            if(PrefabDefinition::RootID(materialized)!=root)return job.Fail("Prefab source changed the instance root identity");
            const auto parent=nodes.at(root)["ParentID"];
            for(auto& node:materialized["Objects"])if(node["ID"].asUInt64()==root)node["ParentID"]=parent;
            Json::Value ordered(Json::arrayValue);
            for(const auto& node:resolved["Objects"]) {
                const auto id=node["ID"].asUInt64();
                if(id==root)for(const auto& replacement:materialized["Objects"])ordered.append(replacement);
                else if(!selected.count(id))ordered.append(node);
            }
            resolved["Objects"].swap(ordered);resolved["NextObjectID"]=materialized["NextObjectID"];
        }
        resolved.removeMember("Annotations");
        if(!SceneDocument::Validate(resolved,error))return job.Fail("Resolved scene Prefab graph is invalid: "+error);
        document.swap(resolved);return true;
    }catch(const std::exception& exception){return job.Fail("Cannot resolve scene Prefab instances: "+std::string(exception.what()));}
    catch(...){return job.Fail("Cannot resolve scene Prefab instances");}
}
