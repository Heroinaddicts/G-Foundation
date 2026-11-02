#pragma once
#include "PrefabDocument.h"
#include <map>
#include <set>
#include <stdexcept>

// Current authoring format. Base/NestedInstances reserve composition boundaries;
// V1 explicitly rejects composing them rather than silently flattening sources.
namespace PrefabDefinition {
constexpr uint64_t InstanceBit=uint64_t(1)<<63;
// JSON text does not encode signed/unsigned storage tags. FileID 4 and UInt64(4)
// are the same identity; compare values without rounding 64-bit integers.
inline bool Equivalent(const Json::Value& a,const Json::Value& b) {
    if(a.isNumeric()&&b.isNumeric()) {
        if(a.isIntegral()&&b.isIntegral()) {
            if(a.isInt64()&&a.asInt64()<0)return b.isInt64()&&a.asInt64()==b.asInt64();
            if(b.isInt64()&&b.asInt64()<0)return false;
            return a.asUInt64()==b.asUInt64();
        }
        return a.asDouble()==b.asDouble();
    }
    if(a.type()!=b.type())return false;
    if(a.isArray()) {if(a.size()!=b.size())return false;for(Json::ArrayIndex i=0;i<a.size();++i)if(!Equivalent(a[i],b[i]))return false;return true;}
    if(a.isObject()) {if(a.size()!=b.size())return false;for(const auto& key:a.getMemberNames())if(!b.isMember(key)||!Equivalent(a[key],b[key]))return false;return true;}
    return a==b;
}
inline uint64_t RootID(const Json::Value& data) {
    for(const auto& node:data["Objects"])if(node.get("ParentID",Json::UInt64(0)).asUInt64()==0)return node["ID"].asUInt64();
    return 0;
}
inline bool Validate(const Json::Value& source,Json::Value& data,std::string& error) {
    if(!source.isObject()||source["Version"]!=1||source["Format"]!="GameSproutPrefab"||
       !source.isMember("Base")||!source["Base"].isNull()||!source["NestedInstances"].isArray()||!source["NestedInstances"].empty()) {
        error="Prefab requires current Version=1, Format=GameSproutPrefab, Base=null and NestedInstances=[]; Nested/Variant are reserved.";return false;
    }
    for(const auto& name:source.getMemberNames())if(name!="Version"&&name!="Format"&&name!="Base"&&name!="NestedInstances"&&name!="Data") {
        error="Unknown Prefab source field: "+name;return false;
    }
    if(!PrefabDocument::Validate(source["Data"],error))return false;
    if(source["Data"].isMember("Annotations")&&!source["Data"]["Annotations"].empty()) {
        error="Prefab template data cannot contain Editor instance annotations";return false;
    }
    std::set<uint64_t> identities;
    for(const auto& node:source["Data"]["Objects"]) {
        const auto accept=[&](const Json::Value& object) {
            return object["ID"].isUInt64()&&object["ID"].asUInt64()>0&&object["ID"].asUInt64()<InstanceBit&&identities.insert(object["ID"].asUInt64()).second;
        };
        if(!accept(node)){error="Prefab node IDs must be unique nonzero source identities";return false;}
        for(const auto& component:node["Components"])if(!accept(component)){error="Prefab components require unique stable IDs";return false;}
    }
    data=source["Data"];return true;
}
inline Json::Value Source(const Json::Value& data) {
    Json::Value result(Json::objectValue);result["Version"]=1;result["Format"]="GameSproutPrefab";
    result["Base"]=Json::Value();result["NestedInstances"]=Json::Value(Json::arrayValue);result["Data"]=data;return result;
}
inline void RemapReferences(Json::Value& data,const std::map<uint64_t,uint64_t>& ids,bool externalToInstance=false,bool externalToScene=false) {
    const auto remap=[&](Json::Value& value,bool zero=true) {
        if(value.isNull())return;
        const auto id=value.asUInt64();if(!id&&zero)return;
        const auto found=ids.find(id);
        if(found!=ids.end())value=Json::UInt64(found->second);
        else if(externalToInstance&&id<InstanceBit)value=Json::UInt64(id|InstanceBit);
        else if(externalToScene&&(id&InstanceBit))value=Json::UInt64(id&~InstanceBit);
        else throw std::runtime_error("Object reference has no identity mapping: "+std::to_string(id));
    };
    if(data.isMember("MainCameraID"))remap(data["MainCameraID"]);
    for(auto& node:data["Objects"]) {
        remap(node["ID"],false);if(node.isMember("ParentID"))remap(node["ParentID"]);
        for(auto& component:node["Components"]) {
            remap(component["ID"],false);
            if(component["Type"]=="GameSproutRuntime.Animator")
                for(auto& binding:component["Bindings"])remap(binding["ObjectFileID"]);
            else if(component["Type"]=="GameSproutRuntime.SkinnedMeshRenderer")remap(component["AnimatorObjectFileID"]);
            else if(component["Type"]=="GameSproutRuntime.ScriptComponent")
                for(auto& field:component["Fields"])if(field["Kind"]=="SceneObject"&&!field["Value"].isNull())remap(field["Value"]["ObjectFileID"],false);
        }
    }
}
inline Json::Value Canonicalize(Json::Value data,bool keepIDs=false,std::map<uint64_t,uint64_t>* outputIDs=nullptr) {
    std::map<uint64_t,uint64_t> ids;uint64_t next=1;
    if(keepIDs&&data["NextObjectID"].isUInt64())next=std::max(next,data["NextObjectID"].asUInt64());
    if(next>=InstanceBit)throw std::runtime_error("Prefab source identity space exhausted");
    for(const auto& node:data["Objects"]) {
        if(keepIDs&&node["ID"].asUInt64()<InstanceBit)next=std::max(next,node["ID"].asUInt64()+1);
        for(const auto& component:node["Components"])if(keepIDs&&component["ID"].asUInt64()<InstanceBit)next=std::max(next,component["ID"].asUInt64()+1);
    }
    for(const auto& node:data["Objects"]) {
        const auto add=[&](const Json::Value& object) {
            const auto id=object["ID"].asUInt64();if(!id||ids.count(id))throw std::runtime_error("Duplicate or missing object/component identity");
            ids[id]=keepIDs&&id<InstanceBit?id:next++;
        };
        add(node);for(const auto& component:node["Components"])add(component);
    }
    RemapReferences(data,ids);
    if(outputIDs)*outputIDs=ids;
    data["NextObjectID"]=Json::UInt64(next);data.removeMember("Annotations");
    data["MainCameraID"]=Json::UInt64(0);data.removeMember("RootMainCamera");
    return data;
}
inline bool IsAtomicReference(const Json::Value& value) {
    return value.isObject()&&((value.isMember("Guid")&&value.isMember("LocalID"))||value.isMember("ObjectFileID"));
}
struct Patch {uint64_t ObjectID=0,ComponentID=0;std::vector<std::string> Path;Json::Value Before,After;bool Remove=false;};
inline void DiffValues(const Json::Value& before,const Json::Value& after,std::vector<std::string> path,
    uint64_t object,uint64_t component,std::vector<Patch>& output) {
    if(Equivalent(before,after))return;
    if(before.isObject()&&after.isObject()&&!IsAtomicReference(before)&&!IsAtomicReference(after)) {
        std::set<std::string> keys;for(const auto& key:before.getMemberNames())keys.insert(key);for(const auto& key:after.getMemberNames())keys.insert(key);
        for(const auto& key:keys) {
            auto child=path;child.push_back(key);
            if(!after.isMember(key))output.push_back({object,component,child,before[key],{},true});
            else if(!before.isMember(key))output.push_back({object,component,child,{},after[key],false});
            else DiffValues(before[key],after[key],child,object,component,output);
        }
    } else if(before.isArray()&&after.isArray()&&before.size()==after.size()) {
        for(Json::ArrayIndex i=0;i<before.size();++i){auto child=path;child.push_back(std::to_string(i));DiffValues(before[i],after[i],child,object,component,output);}
    } else output.push_back({object,component,std::move(path),before,after,false});
}
inline Json::Value NodeFields(Json::Value node,bool root) {
    node.removeMember("ID");node.removeMember("Components");if(root){node.removeMember("ParentID");node.removeMember("Name");}return node;
}
inline Json::Value ComponentFields(Json::Value component,bool root) {
    component.removeMember("ID");
    if(root&&component["Type"]=="GameSproutRuntime.Transform"){component.removeMember("Position");component.removeMember("Rotation");}
    if(root&&component["Type"]=="GameSproutRuntime.RectTransform")component.removeMember("AnchoredPosition");
    return component;
}
inline std::map<uint64_t,Json::Value> Objects(const Json::Value& values) {
    std::map<uint64_t,Json::Value> result;for(const auto& item:values)result.emplace(item["ID"].asUInt64(),item);return result;
}
inline std::vector<Patch> Diff(const Json::Value& baseline,const Json::Value& current) {
    std::vector<Patch> result;const auto old=Objects(baseline["Objects"]),now=Objects(current["Objects"]);const auto root=RootID(baseline);
    for(const auto& [id,node]:old) {
        const auto found=now.find(id);
        if(found==now.end()){result.push_back({id,0,{"$Object"},node,{},true});continue;}
        DiffValues(NodeFields(node,id==root),NodeFields(found->second,id==root),{},id,0,result);
        const auto oldComponents=Objects(node["Components"]),newComponents=Objects(found->second["Components"]);
        for(const auto& [componentID,component]:oldComponents) {
            const auto item=newComponents.find(componentID);
            if(item==newComponents.end())result.push_back({id,componentID,{"$Component"},component,{},true});
            else DiffValues(ComponentFields(component,id==root),ComponentFields(item->second,id==root),{},id,componentID,result);
        }
        for(const auto& [componentID,component]:newComponents)if(!oldComponents.count(componentID))result.push_back({id,componentID,{"$Component"},{},component,false});
    }
    for(const auto& [id,node]:now)if(!old.count(id))result.push_back({id,0,{"$Object"},{},node,false});
    return result;
}
inline void SetPath(Json::Value& value,const Patch& patch) {
    Json::Value* target=&value;
    for(std::size_t i=0;i<patch.Path.size();++i) {
        const auto& key=patch.Path[i];const bool last=i+1==patch.Path.size();
        if(target->isArray()) {
            const auto index=static_cast<Json::ArrayIndex>(std::stoul(key));
            if(index>=target->size())throw std::runtime_error("Override array target no longer exists");
            if(last){if(patch.Remove)throw std::runtime_error("Cannot remove array element as property override");(*target)[index]=patch.After;return;}target=&(*target)[index];
        } else {
            if(!target->isObject())throw std::runtime_error("Override field type changed in source");
            if(last){if(patch.Remove)target->removeMember(key);else (*target)[key]=patch.After;return;}target=&(*target)[key];
        }
    }
}
inline Json::Value Merge(const Json::Value& baseline,const Json::Value& current,const Json::Value& source,const std::vector<Patch>* explicitPatches=nullptr) {
    auto nodes=Objects(source["Objects"]);const auto patches=explicitPatches?*explicitPatches:Diff(baseline,current);
    for(const auto& patch:patches) {
        if(patch.Path==std::vector<std::string>{"$Object"}) {if(patch.Remove)nodes.erase(patch.ObjectID);else nodes[patch.ObjectID]=patch.After;continue;}
        auto node=nodes.find(patch.ObjectID);
        if(node==nodes.end())throw std::runtime_error("Override target was removed from source: "+std::to_string(patch.ObjectID));
        if(!patch.ComponentID){SetPath(node->second,patch);continue;}
        auto components=Objects(node->second["Components"]);
        if(patch.Path==std::vector<std::string>{"$Component"}) {if(patch.Remove)components.erase(patch.ComponentID);else components[patch.ComponentID]=patch.After;}
        else {
            auto component=components.find(patch.ComponentID);
            if(component==components.end())throw std::runtime_error("Overridden component was removed from source");
            SetPath(component->second,patch);
        }
        Json::Value ordered(Json::arrayValue);
        for(const auto& component:node->second["Components"])if(components.count(component["ID"].asUInt64())){ordered.append(components.at(component["ID"].asUInt64()));components.erase(component["ID"].asUInt64());}
        for(const auto& [id,component]:components)ordered.append(component);
        node->second["Components"]=std::move(ordered);
    }
    Json::Value result=source;result["Objects"]=Json::Value(Json::arrayValue);
    for(const auto& node:source["Objects"])if(nodes.count(node["ID"].asUInt64())){result["Objects"].append(nodes.at(node["ID"].asUInt64()));nodes.erase(node["ID"].asUInt64());}
    for(const auto& [id,node]:nodes)result["Objects"].append(node);
    // Root placement/name are scene authoring values, excluded from Apply All.
    const auto currentNodes=Objects(current["Objects"]);const auto root=RootID(source);
    if(currentNodes.count(root))for(auto& node:result["Objects"])if(node["ID"].asUInt64()==root) {
        node["Name"]=currentNodes.at(root)["Name"];
        const auto oldComponents=Objects(currentNodes.at(root)["Components"]);
        for(auto& component:node["Components"])if(oldComponents.count(component["ID"].asUInt64())) {
            const auto& old=oldComponents.at(component["ID"].asUInt64());
            if(component["Type"]=="GameSproutRuntime.Transform")for(const char* field:{"Position","Rotation"}) {
                if(old.isMember(field))component[field]=old[field];else component.removeMember(field);
            }
            if(component["Type"]=="GameSproutRuntime.RectTransform") {
                if(old.isMember("AnchoredPosition"))component["AnchoredPosition"]=old["AnchoredPosition"];else component.removeMember("AnchoredPosition");
            }
        }
    }
    return result;
}
}
