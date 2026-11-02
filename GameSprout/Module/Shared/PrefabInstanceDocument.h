#pragma once
#include "PrefabDefinition.h"
#include "AssetArtifactIO.h"

namespace PrefabRecords {
inline Json::Value Parse(const std::string& text) {
    Json::Value value;
    if(!AssetArtifactIO::Parse(text,value)||!value.isObject())throw std::runtime_error("Invalid scene annotations JSON");
    if(value.isMember("PrefabInstances")&&!value["PrefabInstances"].isArray())throw std::runtime_error("Invalid PrefabInstances annotations");
    return value;
}
inline std::map<uint64_t,uint64_t> Mapping(const Json::Value& record,bool reverse=false) {
    std::map<uint64_t,uint64_t> result;
    if(!record["Mappings"].isArray())throw std::runtime_error("Prefab instance is missing stable mappings");
    for(const auto& item:record["Mappings"]) {
        const auto source=item["SourceID"].asUInt64(),scene=item["SceneID"].asUInt64();
        if(!source||!scene||!result.emplace(reverse?scene:source,reverse?source:scene).second)throw std::runtime_error("Invalid duplicate Prefab identity mapping");
    }
    return result;
}
inline Json::Value Normalize(const Json::Value& record,Json::Value current) {
    auto mapping=Mapping(record,true);
    for(const auto& node:current["Objects"]) {
        const auto add=[&](const Json::Value& object){auto id=object["ID"].asUInt64();if(!mapping.count(id))mapping[id]=id|PrefabDefinition::InstanceBit;};
        add(node);for(const auto& component:node["Components"])add(component);
    }
    PrefabDefinition::RemapReferences(current,mapping,true,false);
    current.removeMember("Annotations");return current;
}
inline Json::Value Materialize(const Json::Value& record,Json::Value data,uint64_t next,Json::Value& mappings) {
    auto mapping=Mapping(record);
    mappings=Json::Value(Json::arrayValue);
    for(const auto& node:data["Objects"]) {
        const auto add=[&](const Json::Value& object,bool component){
            const auto id=object["ID"].asUInt64();
            if(!mapping.count(id)) {
                if(id&PrefabDefinition::InstanceBit)mapping[id]=id&~PrefabDefinition::InstanceBit;
                else {if(!next||next>=PrefabDefinition::InstanceBit)throw std::runtime_error("Scene identity space exhausted");mapping[id]=next++;}
            }
            Json::Value item;item["SourceID"]=Json::UInt64(id);item["SceneID"]=Json::UInt64(mapping.at(id));item["Component"]=component;mappings.append(item);
        };
        add(node,false);for(const auto& component:node["Components"])add(component,true);
    }
    PrefabDefinition::RemapReferences(data,mapping,false,true);
    data["NextObjectID"]=Json::UInt64(next);return data;
}
inline void Put(Json::Value& annotations,const Json::Value& record) {
    Json::Value list(Json::arrayValue);bool replaced=false;
    for(const auto& existing:annotations["PrefabInstances"]) {
        if(existing["RootFileID"]==record["RootFileID"]){list.append(record);replaced=true;}else list.append(existing);
    }
    if(!replaced)list.append(record);annotations["PrefabInstances"]=std::move(list);
}
inline Json::Value Patches(const std::vector<PrefabDefinition::Patch>& patches) {
    Json::Value result(Json::arrayValue);
    for(const auto& patch:patches) {
        Json::Value value;value["ObjectID"]=Json::UInt64(patch.ObjectID);value["ComponentID"]=Json::UInt64(patch.ComponentID);
        value["Path"]=Json::Value(Json::arrayValue);for(const auto& part:patch.Path)value["Path"].append(part);
        value["Before"]=patch.Before;value["After"]=patch.After;value["Remove"]=patch.Remove;result.append(value);
    }
    return result;
}
inline std::vector<PrefabDefinition::Patch> EffectivePatches(const Json::Value& record,const Json::Value& current) {
    auto patches=PrefabDefinition::Diff(record["Baseline"],current);
    const auto nodes=PrefabDefinition::Objects(current["Objects"]);
    for(const auto& value:record["Overrides"]) {
        PrefabDefinition::Patch patch;patch.ObjectID=value["ObjectID"].asUInt64();patch.ComponentID=value["ComponentID"].asUInt64();
        for(const auto& part:value["Path"])patch.Path.push_back(part.asString());
        patch.Before=value["Before"];patch.After=value["After"];patch.Remove=value["Remove"].asBool();
        if(patch.Path.empty()||patch.Path.front().starts_with("$")||patch.Remove)continue;
        bool changed=false;
        for(const auto& other:patches)if(other.ObjectID==patch.ObjectID&&other.ComponentID==patch.ComponentID&&other.Path==patch.Path){changed=true;break;}
        if(changed)continue;
        auto found=nodes.find(patch.ObjectID);if(found==nodes.end())continue;
        Json::Value target=found->second;
        if(patch.ComponentID){auto components=PrefabDefinition::Objects(target["Components"]);auto component=components.find(patch.ComponentID);if(component==components.end())continue;target=component->second;}
        const auto* currentValue=PrefabDefinition::FindPath(target,patch.Path);
        if(currentValue&&PrefabDefinition::Equivalent(*currentValue,patch.After))patches.push_back(std::move(patch));
    }
    return patches;
}
}
