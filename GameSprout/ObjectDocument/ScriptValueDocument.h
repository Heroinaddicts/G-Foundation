#pragma once
#include "RuntimeScriptApi.h"
#include "RuntimeScriptFieldValues.h"
#include "JsonCpp/json.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

// Current ScriptValue wire format shared by source documents and metadata.
// It carries values and file identities only, never native memory addresses.
namespace ScriptValueDocument {
inline void Reject(const std::string& path,const std::string& reason) {
    throw std::runtime_error(path+": "+reason);
}
inline void Members(const Json::Value& value,const std::string& path,std::initializer_list<const char*> allowed) {
    if(!value.isObject())Reject(path,"expected object");
    for(const auto& name:value.getMemberNames()) {
        bool found=false;for(const char* item:allowed)if(name==item){found=true;break;}
        if(!found)Reject(path+"."+name,"unknown field");
    }
}
inline std::uint64_t Unsigned(const Json::Value& value,const std::string& path,bool zero=true) {
    if((value.type()!=Json::intValue&&value.type()!=Json::uintValue)||!value.isUInt64()||(!zero&&!value.asUInt64()))
        Reject(path,"expected unsigned integer");
    return value.asUInt64();
}
inline std::string Text(const Json::Value& value,const std::string& path) {
    if(!value.isString())Reject(path,"expected string");
    auto result=value.asString();
    if(result.size()>1024*1024||result.find('\0')!=std::string::npos)Reject(path,"oversized string or embedded NUL");
    return result;
}
inline double Number(const Json::Value& value,const std::string& path) {
    if(!value.isNumeric()||!std::isfinite(value.asDouble()))Reject(path,"expected finite number");
    return value.asDouble();
}
inline GameSproutRuntime::ScriptValue Parse(const Json::Value& data,const std::string& path="Script.Value",unsigned depth=0) {
    using GameSproutRuntime::ScriptValueKind;
    if(depth>64)Reject(path,"nesting exceeds 64 levels");
    Members(data,path,{"Kind","Value"});
    if(!data.isMember("Value"))Reject(path,"missing Value");
    const auto kind=Text(data["Kind"],path+".Kind");const auto& value=data["Value"];
    GameSproutRuntime::ScriptValue result;
    if(kind=="Boolean") {
        if(!value.isBool())Reject(path,"expected boolean");result.Kind=ScriptValueKind::Boolean;result.Boolean=value.asBool();
    }else if(kind=="Integer") {
        if((value.type()!=Json::intValue&&value.type()!=Json::uintValue)||!value.isInt64())Reject(path,"expected signed 64-bit integer");
        result.Kind=ScriptValueKind::Integer;result.Integer=value.asInt64();
    }else if(kind=="Unsigned") {
        result.Kind=ScriptValueKind::Unsigned;result.Unsigned=Unsigned(value,path);
    }else if(kind=="Number") {result.Kind=ScriptValueKind::Number;result.Number=Number(value,path);
    }else if(kind=="String") {result.Kind=ScriptValueKind::String;result.String=Text(value,path);
    }else if(kind=="Vector2"||kind=="Vector3"||kind=="Vector4"||kind=="Quaternion"||kind=="Color") {
        const unsigned count=kind=="Vector2"?2:kind=="Vector3"?3:4;
        if(!value.isArray()||value.size()!=count)Reject(path,"wrong math component count");
        float numbers[4]{};for(Json::ArrayIndex i=0;i<count;++i) {
            const auto number=Number(value[i],path);
            if(std::fabs(number)>std::numeric_limits<float>::max())Reject(path,"math component exceeds float range");
            numbers[i]=static_cast<float>(number);
        }
        if(kind=="Vector2"){result.Kind=ScriptValueKind::Vector2;result.Vector2={numbers[0],numbers[1]};}
        if(kind=="Vector3"){result.Kind=ScriptValueKind::Vector3;result.Vector={numbers[0],numbers[1],numbers[2]};}
        if(kind=="Vector4"){result.Kind=ScriptValueKind::Vector4;result.Vector4={numbers[0],numbers[1],numbers[2],numbers[3]};}
        if(kind=="Quaternion"){result.Kind=ScriptValueKind::Quaternion;result.Rotation={numbers[0],numbers[1],numbers[2],numbers[3]};}
        if(kind=="Color"){result.Kind=ScriptValueKind::Color;result.Tint={numbers[0],numbers[1],numbers[2],numbers[3]};}
    }else if(kind=="Resource") {
        result.Kind=ScriptValueKind::Resource;
        if(!value.isNull()) {
            Members(value,path,{"Guid","LocalID"});const auto guid=Text(value["Guid"],path+".Guid");
            bool nonzero=false;
            if(guid.size()!=32)Reject(path,"expected 32 hexadecimal GUID digits");
            for(char digit:guid) {
                if(!((digit>='0'&&digit<='9')||(digit>='a'&&digit<='f')))Reject(path,"expected lowercase hexadecimal GUID");
                nonzero=nonzero||digit!='0';
            }
            if(!nonzero)Reject(path,"resource GUID must be nonzero");
            result.Resource={guid,Unsigned(value["LocalID"],path+".LocalID",false)};
        }
    }else if(kind=="SceneObject") {
        result.Kind=ScriptValueKind::SceneObject;
        if(!value.isNull()) {Members(value,path,{"ObjectFileID"});result.SceneObject={true,Unsigned(value["ObjectFileID"],path+".ObjectFileID")};}
    }else if(kind=="Component") {
        result.Kind=ScriptValueKind::Component;
        if(!value.isNull()) {Members(value,path,{"ComponentFileID"});result.Component.Assigned=true;result.Component.ComponentFileID=Unsigned(value["ComponentFileID"],path+".ComponentFileID",false);}
    }else if(kind=="Structure") {
        result.Kind=ScriptValueKind::Structure;
        if(!value.isObject()||value.size()>4096)Reject(path,"expected at most 4096 record members");
        for(const auto& key:value.getMemberNames()) {
            if(key.empty()||key.size()>1024)Reject(path,"invalid record member ID");
            result.Fields.emplace(key,Parse(value[key],path+"."+key,depth+1));
        }
    }else if(kind=="Sequence") {
        result.Kind=ScriptValueKind::Sequence;
        if(!value.isArray()||value.size()>100000)Reject(path,"expected at most 100000 elements");
        for(Json::ArrayIndex i=0;i<value.size();++i)result.Elements.push_back(Parse(value[i],path+"["+std::to_string(i)+"]",depth+1));
    }else if(kind=="Dictionary") {
        result.Kind=ScriptValueKind::Dictionary;
        if(!value.isArray()||value.size()>100000)Reject(path,"expected at most 100000 dictionary entries");
        for(Json::ArrayIndex i=0;i<value.size();++i) {
            const auto entryPath=path+"["+std::to_string(i)+"]";Members(value[i],entryPath,{"Key","Value"});
            result.Entries.emplace_back(Parse(value[i]["Key"],entryPath+".Key",depth+1),Parse(value[i]["Value"],entryPath+".Value",depth+1));
        }
    }else Reject(path,"unknown script value kind");
    if(!depth) {
        std::string error;
        if(!GameSproutRuntime::ValidateScriptValue(result,error,true))Reject(path,error);
    }
    return result;
}
inline Json::Value Encode(const GameSproutRuntime::ScriptValue& value,unsigned depth=0) {
    using GameSproutRuntime::ScriptValueKind;
    if(depth>64)Reject("Script.Value","nesting exceeds 64 levels");
    Json::Value data(Json::objectValue);auto& output=data["Value"];
    switch(value.Kind) {
    case ScriptValueKind::Boolean:data["Kind"]="Boolean";output=value.Boolean;break;
    case ScriptValueKind::Integer:data["Kind"]="Integer";output=Json::Int64(value.Integer);break;
    case ScriptValueKind::Unsigned:data["Kind"]="Unsigned";output=Json::UInt64(value.Unsigned);break;
    case ScriptValueKind::Number:data["Kind"]="Number";output=value.Number;break;
    case ScriptValueKind::String:data["Kind"]="String";output=value.String;break;
    case ScriptValueKind::Vector2:
        data["Kind"]="Vector2";output=Json::Value(Json::arrayValue);output.append(value.Vector2.X);output.append(value.Vector2.Y);break;
    case ScriptValueKind::Vector3:
        data["Kind"]="Vector3";output=Json::Value(Json::arrayValue);output.append(value.Vector.X);output.append(value.Vector.Y);output.append(value.Vector.Z);break;
    case ScriptValueKind::Vector4:
        data["Kind"]="Vector4";output=Json::Value(Json::arrayValue);output.append(value.Vector4.X);output.append(value.Vector4.Y);output.append(value.Vector4.Z);output.append(value.Vector4.W);break;
    case ScriptValueKind::Quaternion:
        data["Kind"]="Quaternion";output=Json::Value(Json::arrayValue);output.append(value.Rotation.X);output.append(value.Rotation.Y);output.append(value.Rotation.Z);output.append(value.Rotation.W);break;
    case ScriptValueKind::Color:
        data["Kind"]="Color";output=Json::Value(Json::arrayValue);output.append(value.Tint.R);output.append(value.Tint.G);output.append(value.Tint.B);output.append(value.Tint.A);break;
    case ScriptValueKind::Resource:
        data["Kind"]="Resource";
        if(!value.Resource.Guid.empty()){output["Guid"]=value.Resource.Guid;output["LocalID"]=Json::UInt64(value.Resource.LocalID);}
        else if(value.Resource.LocalID)Reject("Script.Value","unbound resource has a LocalID");
        break;
    case ScriptValueKind::SceneObject:
        data["Kind"]="SceneObject";
        if(value.SceneObject.RuntimeSceneID||value.SceneObject.RuntimeObjectID)Reject("Script.Value","runtime object references cannot be persisted");
        if(value.SceneObject.Assigned)output["ObjectFileID"]=Json::UInt64(value.SceneObject.ObjectFileID);
        else if(value.SceneObject.ObjectFileID)Reject("Script.Value","unassigned object reference has a FileID");
        break;
    case ScriptValueKind::Component:
        data["Kind"]="Component";
        if(value.Component.RuntimeSceneID||value.Component.RuntimeObjectID||value.Component.RuntimeComponentID)Reject("Script.Value","runtime component references cannot be persisted");
        if(value.Component.Assigned)output["ComponentFileID"]=Json::UInt64(value.Component.ComponentFileID);
        else if(value.Component.ComponentFileID)Reject("Script.Value","unassigned component reference has a FileID");
        break;
    case ScriptValueKind::Structure:
        data["Kind"]="Structure";output=Json::Value(Json::objectValue);
        for(const auto& [key,child]:value.Fields)output[key]=Encode(child,depth+1);break;
    case ScriptValueKind::Sequence:
        data["Kind"]="Sequence";output=Json::Value(Json::arrayValue);
        for(const auto& child:value.Elements)output.append(Encode(child,depth+1));break;
    case ScriptValueKind::Dictionary:
        data["Kind"]="Dictionary";output=Json::Value(Json::arrayValue);
        for(const auto& [key,child]:value.Entries) {Json::Value entry(Json::objectValue);entry["Key"]=Encode(key,depth+1);entry["Value"]=Encode(child,depth+1);output.append(std::move(entry));}break;
    default:Reject("Script.Value","invalid script value kind");
    }
    return data;
}
inline Json::Value Serialize(const GameSproutRuntime::ScriptValue& value) {
    std::string error;
    if(!GameSproutRuntime::ValidateScriptValue(value,error,true))Reject("Script.Value",error);
    auto data=Encode(value);Parse(data);return data;
}
}
