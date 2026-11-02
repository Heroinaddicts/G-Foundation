#include "SceneScriptDocument.h"
#include "RuntimeScriptFieldValues.h"
#include "ScriptValueDocument.h"
#include <stdexcept>

namespace {
[[noreturn]] void Reject(const std::string& path,const std::string& reason) {
    throw std::runtime_error(path+": "+reason);
}
void Members(const Json::Value& value,const std::string& path,std::initializer_list<const char*> allowed) {
    if(!value.isObject())Reject(path,"expected object");
    for(const auto& name:value.getMemberNames()) {
        bool found=false;
        for(const auto* item:allowed)if(name==item){found=true;break;}
        if(!found)Reject(path+"."+name,"unknown field");
    }
}
std::string Text(const Json::Value& value,const std::string& path,std::size_t limit,bool empty=false) {
    if(!value.isString())Reject(path,"expected string");
    const auto result=value.asString();
    if((!empty&&result.empty())||result.size()>limit||result.find('\0')!=std::string::npos)
        Reject(path,"invalid string length or NUL character");
    return result;
}
std::uint64_t ID(const Json::Value& value,const std::string& path) {
    if((value.type()!=Json::intValue&&value.type()!=Json::uintValue)||!value.isUInt64()||!value.asUInt64())
        Reject(path,"expected nonzero unsigned integer");
    return value.asUInt64();
}
GameSproutRuntime::ResourceReference Reference(const Json::Value& value,const std::string& path) {
    if(value.isNull())return {};
    Members(value,path,{"Guid","LocalID"});
    const auto guid=Text(value["Guid"],path+".Guid",32);
    if(guid.size()!=32)Reject(path,"expected 32 hexadecimal GUID digits");
    bool nonzero=false;
    for(const char digit:guid) {
        if(!((digit>='0'&&digit<='9')||(digit>='a'&&digit<='f')))Reject(path,"expected lowercase hexadecimal GUID");
        nonzero=nonzero||digit!='0';
    }
    if(!nonzero)Reject(path,"resource GUID must be nonzero");
    return {guid,ID(value["LocalID"],path+".LocalID")};
}
Json::Value Reference(const GameSproutRuntime::ResourceReference& value) {
    if(value.Guid.empty()) {
        if(value.LocalID)Reject("Script.Source","unbound resource has a nonzero LocalID");
        return {};
    }
    Json::Value data(Json::objectValue);data["Guid"]=value.Guid;data["LocalID"]=Json::UInt64(value.LocalID);
    Reference(data,"Script.Resource");return data;
}
GameSproutRuntime::ScriptValue Value(const Json::Value& data,const std::string& path) {
    return ScriptValueDocument::Parse(data,path);
}
Json::Value Value(const GameSproutRuntime::ScriptValue& value) {
    return ScriptValueDocument::Serialize(value);
}
}

namespace SceneScriptDocument {
bool AlignFields(Json::Value& document,
    const std::vector<GameSproutRuntime::ScriptTypeInfo>& schemas,std::string& error) {
    error.clear();
    try {
        if(!document.isObject()||!document["Objects"].isArray())Reject("Script.Document","expected object graph");
        std::map<std::string,const GameSproutRuntime::ScriptTypeInfo*> types;
        for(const auto& schema:schemas)if(!types.emplace(schema.TypeID,&schema).second)
            Reject("Script.Schema","duplicate script type");
        Json::Value candidate=document;
        const auto align=[&](Json::Value& components) {
            for(auto& component:components) {
                if(component["Type"]!="GameSproutRuntime.ScriptComponent")continue;
                Validate(component,"Script");
                const auto schema=types.find(component["ScriptTypeID"].asString());
                if(schema==types.end())continue;
                std::map<std::string,GameSproutRuntime::ScriptValue> stored;
                for(const auto& key:component["Fields"].getMemberNames())
                    stored.emplace(key,Value(component["Fields"][key],"Script.Fields."+key));
                const auto projected=GameSproutRuntime::ProjectSerializedScriptFields(stored,schema->second);
                Json::Value fields(Json::objectValue);
                for(const auto& [key,value]:projected)fields[key]=Value(value);
                component["Fields"].swap(fields);
            }
        };
        if(candidate.isMember("RootComponents"))align(candidate["RootComponents"]);
        for(auto& node:candidate["Objects"])align(node["Components"]);
        document.swap(candidate);return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
    catch(...){error="Cannot align serialized script fields";return false;}
}
void Validate(const Json::Value& data,const std::string& path) {
    Members(data,path,{"ID","Type","InstanceID","ScriptTypeID","Source","Enabled","Fields"});
    if(data.isMember("ID"))ID(data["ID"],path+".ID");
    if(data["Type"]!="GameSproutRuntime.ScriptComponent")Reject(path,"invalid script component type");
    ID(data["InstanceID"],path+".InstanceID");Text(data["ScriptTypeID"],path+".ScriptTypeID",1024);
    if(Reference(data["Source"],path+".Source").Guid.empty())Reject(path+".Source","script source asset is required");
    if(!data["Enabled"].isBool())Reject(path+".Enabled","expected boolean");
    if(!data["Fields"].isObject()||data["Fields"].size()>4096)Reject(path+".Fields","expected at most 4096 fields");
    for(const auto& key:data["Fields"].getMemberNames()) {
        Text(Json::Value(key),path+".Fields key",1024);
        Value(data["Fields"][key],path+".Fields."+key);
    }
}
void Create(const Json::Value& data,GameSproutRuntime::IGameObject& object,GameSproutRuntime::IGameSproutRuntime& runtime) {
    Validate(data,"Script");
    std::string error;
    GameSproutRuntime::IScriptComponent* component=nullptr;
    for(std::size_t index=0;index<object.GetComponentCount();++index)
        if(auto* candidate=dynamic_cast<GameSproutRuntime::IScriptComponent*>(object.GetComponentAt(index));
            candidate&&candidate->GetScriptInstanceID()==data["InstanceID"].asUInt64()) {component=candidate;break;}
    const bool existing=component!=nullptr;
    if(existing) {
        const auto source=Reference(data["Source"],"Script.Source");
        if(component->GetScriptTypeID()!=data["ScriptTypeID"].asString()||component->GetScriptSource()!=source)
            Reject("Script","existing component has another script identity");
    }else component=runtime.AddScriptComponent(&object,data["ScriptTypeID"].asString(),
        Reference(data["Source"],"Script.Source"),data["InstanceID"].asUInt64(),error);
    if(!component)Reject("Script",error.empty()?"Runtime rejected script component":error);
    try {
        std::map<std::string,GameSproutRuntime::ScriptValue> fields;
        for(const auto& key:data["Fields"].getMemberNames())
            fields.emplace(key,Value(data["Fields"][key],"Script.Fields."+key));
        if(!component->RestoreSerializedFields(fields,error))Reject("Script.Fields",error);
        component->SetEnabled(data["Enabled"].asBool());
    } catch(...) {
        if(!existing)runtime.RemoveScriptComponent(&object,component->GetScriptInstanceID(),error);throw;
    }
}
Json::Value Serialize(GameSproutRuntime::IComponent& value) {
    auto* component=dynamic_cast<GameSproutRuntime::IScriptComponent*>(&value);
    if(!component)Reject("Script","script component interface mismatch");
    Json::Value data(Json::objectValue);data["Type"]="GameSproutRuntime.ScriptComponent";
    data["InstanceID"]=Json::UInt64(component->GetScriptInstanceID());data["ScriptTypeID"]=component->GetScriptTypeID();
    data["Source"]=Reference(component->GetScriptSource());data["Enabled"]=component->IsEnabled();
    data["Fields"]=Json::Value(Json::objectValue);
    for(const auto& field:component->GetFields())data["Fields"][field.first]=Value(field.second);
    Validate(data,"Script");return data;
}
}
