#pragma once
#include "../Interface/AnimationImportSettings.h"
#include <JsonCpp/json.hpp>
#include <cmath>
#include <set>
#include <limits>

namespace AnimationImportSettingsJson {
inline bool Fields(const Json::Value& value,std::initializer_list<const char*> fields) {
    if(!value.isObject())return false;
    for(const auto& key:value.getMemberNames()) {
        bool found=false;for(const char* field:fields)if(key==field){found=true;break;}
        if(!found)return false;
    }
    return true;
}
inline bool Text(const std::string& text,std::size_t maximum,bool empty=true) {
    return (empty||!text.empty())&&text.size()<=maximum&&text.find('\0')==std::string::npos;
}
inline bool Validate(const AnimationClipImportOptions& value,bool model) {
    if(!Text(value.Name,1024,!model)||(!model&&!value.Name.empty())||!Text(value.RootMotionNode,65536)
        ||(value.ExtractRootMotion&&value.RootMotionNode.empty())||value.Events.size()>4096)return false;
    float previous=-1;
    for(const auto& event:value.Events) {
        if(!std::isfinite(event.Time)||event.Time<0||event.Time<previous||!Text(event.Name,256,false)||!Text(event.Payload,65536))return false;
        previous=event.Time;
    }
    return true;
}
inline Json::Value Write(const AnimationClipImportOptions& value,bool model) {
    Json::Value result(Json::objectValue);
    if(model)result["Name"]=value.Name;
    result["ExtractRootMotion"]=value.ExtractRootMotion;result["RootMotionNode"]=value.RootMotionNode;
    result["Events"]=Json::Value(Json::arrayValue);
    for(const auto& event:value.Events) {
        Json::Value data(Json::objectValue);data["Time"]=event.Time;data["Name"]=event.Name;data["Payload"]=event.Payload;
        result["Events"].append(std::move(data));
    }
    return result;
}
inline bool Read(const Json::Value& source,AnimationClipImportOptions& output,bool model) {
    if(!Fields(source,model?std::initializer_list<const char*>{"Name","ExtractRootMotion","RootMotionNode","Events"}:
                              std::initializer_list<const char*>{"ExtractRootMotion","RootMotionNode","Events"}))return false;
    AnimationClipImportOptions value;
    if(model){if(!source["Name"].isString())return false;value.Name=source["Name"].asString();}
    if(source.isMember("ExtractRootMotion")){if(!source["ExtractRootMotion"].isBool())return false;value.ExtractRootMotion=source["ExtractRootMotion"].asBool();}
    if(source.isMember("RootMotionNode")){if(!source["RootMotionNode"].isString())return false;value.RootMotionNode=source["RootMotionNode"].asString();}
    if(source.isMember("Events")) {
        if(!source["Events"].isArray()||source["Events"].size()>4096)return false;
        for(const auto& item:source["Events"]) {
            if(!Fields(item,{"Time","Name","Payload"})||!item["Time"].isNumeric()||!item["Name"].isString()
                ||(item.isMember("Payload")&&!item["Payload"].isString()))return false;
            const double time=item["Time"].asDouble();
            if(!std::isfinite(time)||time<0||time>std::numeric_limits<float>::max())return false;
            value.Events.push_back({static_cast<float>(time),item["Name"].asString(),item.get("Payload","").asString()});
        }
    }
    if(!Validate(value,model))return false;output=std::move(value);return true;
}
inline bool ReadModel(const Json::Value& source,std::vector<AnimationClipImportOptions>& output) {
    if(!source.isArray()||source.size()>4096)return false;
    std::vector<AnimationClipImportOptions> values;std::set<std::string> names;
    for(const auto& entry:source){AnimationClipImportOptions value;if(!Read(entry,value,true)||!names.insert(value.Name).second)return false;values.push_back(std::move(value));}
    output=std::move(values);return true;
}
}
