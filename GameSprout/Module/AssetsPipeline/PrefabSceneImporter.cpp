#include "AssetImportJob.h"
#include "../Shared/PrefabInstanceDocument.h"
#include <algorithm>
#include <limits>

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
using ObjectRow = Json::ArrayIndex;
using ChildRows = std::map<std::uint64_t,std::vector<ObjectRow>>;

Json::Value Capture(const Json::Value& scene,const ChildRows& children,ObjectRow root,
    std::uint64_t next,std::vector<bool>& visited,std::vector<ObjectRow>& selected) {
    const auto& objects = scene["Objects"];
    const auto camera = scene.get("MainCameraID",Json::UInt64(0)).asUInt64();
    bool includesCamera = false;
    std::vector<ObjectRow> pending{root};
    while(!pending.empty()) {
        const auto row=pending.back();pending.pop_back();
        if(visited[row])throw std::runtime_error("Nested or overlapping scene Prefab instances are reserved");
        visited[row]=true;
        selected.push_back(row);
        const auto id=objects[row]["ID"].asUInt64();
        includesCamera |= id == camera;
        if(const auto found=children.find(id);found!=children.end())pending.insert(pending.end(),found->second.begin(),found->second.end());
    }
    std::sort(selected.begin(),selected.end()); // Keep the scene's authored row order.
    Json::Value result(Json::objectValue);result["Version"]=GameSproutRuntime::SceneDocumentVersion;result["Name"]="Prefab instance";
    result["NextObjectID"]=Json::UInt64(next);
    result["MainCameraID"]=Json::UInt64(includesCamera?camera:0);
    result["Objects"]=Json::Value(Json::arrayValue);
    for(const auto row:selected) {
        auto node=objects[row];
        if(row==root)node["ParentID"]=Json::UInt64(0);
        result["Objects"].append(std::move(node));
    }
    return result;
}

bool SameValidatedValue(const Json::Value& before,const Json::Value& after) {
    if(before.type()!=after.type()) {
        // Remapping file IDs can change their integer storage tag. The Scene
        // validator accepts either tag for the same nonnegative integer.
        const auto integral=[](Json::ValueType type){return type==Json::intValue||type==Json::uintValue;};
        return integral(before.type())&&integral(after.type())&&before.isUInt64()&&after.isUInt64()
            &&before.asUInt64()==after.asUInt64();
    }
    if(before.isObject()) {
        if(before.size()!=after.size())return false;
        for(auto member=before.begin();member!=before.end();++member) {
            const auto key=member.name();
            if(!after.isMember(key)||!SameValidatedValue(*member,after[key]))return false;
        }
        return true;
    }
    if(before.isArray()) {
        if(before.size()!=after.size())return false;
        for(ObjectRow row=0;row<before.size();++row)if(!SameValidatedValue(before[row],after[row]))return false;
        return true;
    }
    return before==after;
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
        const Json::Value& sourceDocument=document;
        const auto& annotations=sourceDocument["Annotations"];
        if(annotations.isMember("PrefabInstances")&&!annotations["PrefabInstances"].isArray())
            return job.Fail("Scene PrefabInstances annotations must be an array");
        if(annotations["PrefabInstances"].empty()) {
            // The common ordinary scene already passed full validation above.
            // Removing author-only records cannot change its Runtime graph.
            document.removeMember("Annotations");
            return true;
        }
        const auto& objects=sourceDocument["Objects"];
        ChildRows children;
        std::map<std::uint64_t,ObjectRow> index;
        ObjectRow row=0;
        for(const auto& node:objects) {
            index.emplace(node["ID"].asUInt64(),row);
            children[node["ParentID"].asUInt64()].push_back(row++);
        }
        const auto previousNext=NextIdentity(sourceDocument);
        auto next=previousNext;
        std::vector<bool> visited(objects.size()),replaced(objects.size());
        std::map<ObjectRow,Json::Value> replacements;
        bool graphUnchanged=true;
        for(const auto& record:annotations["PrefabInstances"]) {
            if(job.Cancelled())return false;
            CheckRecord(record);
            const auto root=record["RootFileID"].asUInt64();
            const auto position=index.find(root);
            // Deleting an instance can leave its authoring record until the next
            // editor reconciliation. It must never resurrect the deleted root.
            if(position==index.end())continue;
            const auto rootRow=position->second;
            std::vector<ObjectRow> selected;
            auto current=Capture(sourceDocument,children,rootRow,next,visited,selected);
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
            Json::Value source,data;
            if(!AssetArtifactIO::Parse(bytes.toStdString(),source)||!PrefabDefinition::Validate(source,data,error))
                return job.Fail("Invalid Prefab scene source "+sourcePath+": "+error);
            const auto normalized=PrefabRecords::Normalize(record,std::move(current));
            const auto patches=PrefabRecords::EffectivePatches(record,normalized);
            const auto merged=PrefabDefinition::Merge(record["Baseline"],normalized,data,&patches);
            Json::Value mappings;
            auto materialized=PrefabRecords::Materialize(record,merged,next,mappings);
            if(PrefabDefinition::RootID(materialized)!=root)return job.Fail("Prefab source changed the instance root identity");
            auto& rows=materialized["Objects"];
            for(auto& node:rows)if(node["ID"].asUInt64()==root)node["ParentID"]=objects[rootRow]["ParentID"];
            if(graphUnchanged) {
                graphUnchanged=selected.front()==rootRow&&selected.size()==rows.size();
                for(ObjectRow offset=0;graphUnchanged&&offset<rows.size();++offset)
                    graphUnchanged=selected[offset]==rootRow+offset&&SameValidatedValue(objects[selected[offset]],rows[offset]);
            }
            next=materialized["NextObjectID"].asUInt64();
            for(const auto row:selected)replaced[row]=true;
            replacements.emplace(rootRow,std::move(rows));
        }
        document.removeMember("Annotations");
        if(replacements.empty())return true;

        auto& destination=document["Objects"];
        document["NextObjectID"]=Json::UInt64(next);
        if(graphUnchanged&&next==previousNext) {
            // These replacements have the validated values at the same rows.
            // Retain the normal materialized integer tags in the binary output.
            for(auto& [root,rows]:replacements)for(ObjectRow offset=0;offset<rows.size();++offset)
                destination[root+offset]=std::move(rows[offset]);
        } else {
            // Move untouched nodes once, regardless of the number of Prefab instances.
            Json::Value ordered(Json::arrayValue);
            row=0;
            for(auto& original:destination) {
                const auto found=replacements.find(row);
                if(found!=replacements.end())for(auto& node:found->second)ordered.append(std::move(node));
                else if(!replaced[row])ordered.append(std::move(original));
                ++row;
            }
            destination.swap(ordered);
            if(!SceneDocument::Validate(document,error))return job.Fail("Resolved scene Prefab graph is invalid: "+error);
        }
        return true;
    }catch(const std::exception& exception){return job.Fail("Cannot resolve scene Prefab instances: "+std::string(exception.what()));}
    catch(...){return job.Fail("Cannot resolve scene Prefab instances");}
}
