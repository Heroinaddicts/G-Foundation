#include "PrefabScriptFields.h"
#include <set>

namespace {
const Json::Value* Component(const Json::Value& document,uint64_t objectID,uint64_t componentID) {
    for(const auto& object:document["Objects"])if(object["ID"].asUInt64()==objectID)
        for(const auto& component:object["Components"])if(component["ID"].asUInt64()==componentID)return &component;
    return nullptr;
}
bool KeepOverride(const Json::Value& value,const Json::Value& before,const Json::Value& current,
    const std::set<std::string>& known) {
    const auto objectID=value["ObjectID"].asUInt64(),componentID=value["ComponentID"].asUInt64();
    const auto* component=Component(current,objectID,componentID);
    if(!component||(*component)["Type"]!="GameSproutRuntime.ScriptComponent"||
       !known.count((*component)["ScriptTypeID"].asString()))return true;
    const auto& path=value["Path"];
    if(path.empty()||path[0]!="Fields")return true;
    if(path.size()<2)return false;
    const auto fieldID=path[1].asString();
    const auto& fields=(*component)["Fields"];
    if(!fields.isMember(fieldID))return false;
    const auto& kind=fields[fieldID]["Kind"];
    if(const auto* original=Component(before,objectID,componentID)) {
        const auto& previous=(*original)["Fields"];
        if(previous.isMember(fieldID)&&previous[fieldID]["Kind"]!=kind)return false;
    }
    // Kind belongs to the declaration; it cannot be a user-authored override.
    if(path.size()>2&&path[2]=="Kind")return false;
    if(path.size()==2&&!value["Remove"].asBool()&&value["After"]["Kind"]!=kind)return false;
    return true;
}
}
namespace PrefabScriptFields {
bool Align(Json::Value& record,Json::Value& current,Json::Value* source,
    const std::vector<GameSproutRuntime::ScriptTypeInfo>& schemas,std::string& error) {
    const auto before=record["Baseline"];
    if(!SceneScriptDocument::AlignFields(record["Baseline"],schemas,error)||
       !SceneScriptDocument::AlignFields(current,schemas,error)||
       (source&&!SceneScriptDocument::AlignFields(*source,schemas,error)))return false;
    std::set<std::string> known;for(const auto& schema:schemas)known.insert(schema.TypeID);
    Json::Value retained(Json::arrayValue);
    for(const auto& value:record["Overrides"])if(KeepOverride(value,before,current,known))retained.append(value);
    record["Overrides"]=std::move(retained);return true;
}
}
