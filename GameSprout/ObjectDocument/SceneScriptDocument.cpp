#include "SceneScriptDocument.h"
#include "RuntimeScriptFieldValues.h"
#include <cmath>
#include <limits>
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
double Number(const Json::Value& value,const std::string& path) {
    if(!value.isNumeric()||!std::isfinite(value.asDouble()))Reject(path,"expected finite number");
    return value.asDouble();
}
GameSproutRuntime::ScriptValue Value(const Json::Value& data,const std::string& path) {
    Members(data,path,{"Kind","Value"});
    if(!data.isMember("Value"))Reject(path,"missing field value");
    const auto kind=Text(data["Kind"],path+".Kind",32);
    const auto& value=data["Value"];
    GameSproutRuntime::ScriptValue result;
    if(kind=="Boolean") {
        if(!value.isBool())Reject(path,"expected boolean");
        result.Kind=GameSproutRuntime::ScriptValueKind::Boolean;result.Boolean=value.asBool();
    } else if(kind=="Integer") {
        if((value.type()!=Json::intValue&&value.type()!=Json::uintValue)||!value.isInt64())Reject(path,"expected signed 64-bit integer");
        result.Kind=GameSproutRuntime::ScriptValueKind::Integer;result.Integer=value.asInt64();
    } else if(kind=="Number") {
        result.Kind=GameSproutRuntime::ScriptValueKind::Number;result.Number=Number(value,path);
    } else if(kind=="String") {
        result.Kind=GameSproutRuntime::ScriptValueKind::String;result.String=Text(value,path,1024*1024,true);
    } else if(kind=="Vector3") {
        if(!value.isArray()||value.size()!=3)Reject(path,"expected three float components");
        float numbers[3];
        for(Json::ArrayIndex i=0;i<3;++i) {
            const auto number=Number(value[i],path);
            if(std::fabs(number)>std::numeric_limits<float>::max())Reject(path,"vector component exceeds float range");
            numbers[i]=static_cast<float>(number);
        }
        result.Kind=GameSproutRuntime::ScriptValueKind::Vector3;result.Vector={numbers[0],numbers[1],numbers[2]};
    } else if(kind=="Resource") {
        result.Kind=GameSproutRuntime::ScriptValueKind::Resource;result.Resource=Reference(value,path);
    } else if(kind=="SceneObject") {
        result.Kind=GameSproutRuntime::ScriptValueKind::SceneObject;
        if(!value.isNull()) {
            Members(value,path+".Value",{"ObjectFileID"});
            const auto& id=value["ObjectFileID"];
            if((id.type()!=Json::intValue&&id.type()!=Json::uintValue)||!id.isUInt64())
                Reject(path,"expected an unsigned scene object file ID");
            result.SceneObject={true,id.asUInt64()};
        }
    } else Reject(path,"unknown script value kind");
    return result;
}
Json::Value Value(const GameSproutRuntime::ScriptValue& value) {
    Json::Value data(Json::objectValue);
    switch(value.Kind) {
    case GameSproutRuntime::ScriptValueKind::Boolean:data["Kind"]="Boolean";data["Value"]=value.Boolean;break;
    case GameSproutRuntime::ScriptValueKind::Integer:data["Kind"]="Integer";data["Value"]=Json::Int64(value.Integer);break;
    case GameSproutRuntime::ScriptValueKind::Number:data["Kind"]="Number";data["Value"]=value.Number;break;
    case GameSproutRuntime::ScriptValueKind::String:data["Kind"]="String";data["Value"]=value.String;break;
    case GameSproutRuntime::ScriptValueKind::Vector3:
        data["Kind"]="Vector3";data["Value"]=Json::Value(Json::arrayValue);
        data["Value"].append(value.Vector.X);data["Value"].append(value.Vector.Y);data["Value"].append(value.Vector.Z);break;
    case GameSproutRuntime::ScriptValueKind::Resource:data["Kind"]="Resource";data["Value"]=Reference(value.Resource);break;
    case GameSproutRuntime::ScriptValueKind::SceneObject:
        data["Kind"]="SceneObject";
        if(value.SceneObject.Assigned)data["Value"]["ObjectFileID"]=Json::UInt64(value.SceneObject.ObjectFileID);
        else {
            if(value.SceneObject.ObjectFileID)Reject("Script.Fields","unassigned object reference has a file ID");
            data["Value"]=Json::Value();
        }
        break;
    default:Reject("Script.Fields","invalid script value kind");
    }
    Value(data,"Script.Fields");return data;
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
