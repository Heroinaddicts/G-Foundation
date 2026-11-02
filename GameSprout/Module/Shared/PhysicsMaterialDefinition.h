#pragma once
#include "RuntimePhysicsData.h"
#include "JsonCpp/json.hpp"
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// Current authoring format and its fixed 16-byte LE runtime representation.
// Backend handles and identity belong to Runtime / ArtifactDB, never this payload.
namespace PhysicsMaterialDefinition {
inline const char* CombineName(GameSproutRuntime::PhysicsMaterialCombine mode) {
    switch(mode) {
    case GameSproutRuntime::PhysicsMaterialCombine::Average:return "Average";
    case GameSproutRuntime::PhysicsMaterialCombine::Minimum:return "Minimum";
    case GameSproutRuntime::PhysicsMaterialCombine::Multiply:return "Multiply";
    case GameSproutRuntime::PhysicsMaterialCombine::Maximum:return "Maximum";
    }
    return "Invalid";
}
inline bool Valid(const GameSproutRuntime::PhysicsMaterialData& data,std::string& error) {
    error.clear();
    if(!std::isfinite(data.Friction)||data.Friction<0 || !std::isfinite(data.Restitution)||data.Restitution<0||data.Restitution>1) {
        error="Physics material requires finite nonnegative Friction and Restitution in [0,1]";return false;
    }
    if(std::string(CombineName(data.FrictionCombine))=="Invalid"||std::string(CombineName(data.RestitutionCombine))=="Invalid") {
        error="Physics material contains an unknown combine mode";return false;
    }
    return true;
}
inline bool Read(const Json::Value& source,GameSproutRuntime::PhysicsMaterialData& output,std::string& error) {
    error.clear();
    if(!source.isObject()){error="Physics material must be an object";return false;}
    for(const auto& key:source.getMemberNames())
        if(key!="Version"&&key!="Friction"&&key!="Restitution"&&key!="FrictionCombine"&&key!="RestitutionCombine") {
            error="Unknown physics material field: "+key;return false;
        }
    if(source["Version"]!=1){error="Physics material Version must be 1";return false;}
    GameSproutRuntime::PhysicsMaterialData data;
    const auto number=[&](const char* name,float& target) {
        if(!source.isMember(name))return true;
        if(!source[name].isNumeric()||!std::isfinite(source[name].asDouble())||
            std::fabs(source[name].asDouble())>std::numeric_limits<float>::max())return false;
        target=source[name].asFloat();return true;
    };
    const auto mode=[&](const char* name,GameSproutRuntime::PhysicsMaterialCombine& target) {
        if(!source.isMember(name))return true;
        if(!source[name].isString())return false;
        for(const auto candidate:{GameSproutRuntime::PhysicsMaterialCombine::Average,GameSproutRuntime::PhysicsMaterialCombine::Minimum,
                                 GameSproutRuntime::PhysicsMaterialCombine::Multiply,GameSproutRuntime::PhysicsMaterialCombine::Maximum})
            if(source[name].asString()==CombineName(candidate)){target=candidate;return true;}
        return false;
    };
    if(!number("Friction",data.Friction)||!number("Restitution",data.Restitution)||
       !mode("FrictionCombine",data.FrictionCombine)||!mode("RestitutionCombine",data.RestitutionCombine)) {
        error="Invalid physics material numeric value or combine mode";return false;
    }
    if(!Valid(data,error))return false;
    output=data;return true;
}
inline Json::Value Write(const GameSproutRuntime::PhysicsMaterialData& data) {
    Json::Value value(Json::objectValue);value["Version"]=1;
    value["Friction"]=data.Friction;value["Restitution"]=data.Restitution;
    value["FrictionCombine"]=CombineName(data.FrictionCombine);value["RestitutionCombine"]=CombineName(data.RestitutionCombine);
    return value;
}
inline bool Encode(const GameSproutRuntime::PhysicsMaterialData& data,std::vector<std::uint8_t>& output,std::string& error) {
    if(!Valid(data,error))return false;
    std::vector<std::uint8_t> bytes;bytes.reserve(16);
    const auto append=[&](std::uint32_t value){for(unsigned i=0;i<4;++i)bytes.push_back(static_cast<std::uint8_t>(value>>(i*8)));};
    append(std::bit_cast<std::uint32_t>(data.Friction));append(std::bit_cast<std::uint32_t>(data.Restitution));
    append(static_cast<std::uint32_t>(data.FrictionCombine));append(static_cast<std::uint32_t>(data.RestitutionCombine));
    output.swap(bytes);return true;
}
inline bool Decode(const std::uint8_t* bytes,std::size_t length,GameSproutRuntime::PhysicsMaterialData& output,std::string& error) {
    if(!bytes||length!=16){error="Prepared physics material must contain exactly 16 bytes";return false;}
    const auto read=[&](unsigned offset){std::uint32_t result=0;for(unsigned i=0;i<4;++i)result|=std::uint32_t(bytes[offset+i])<<(i*8);return result;};
    GameSproutRuntime::PhysicsMaterialData data;
    data.Friction=std::bit_cast<float>(read(0));data.Restitution=std::bit_cast<float>(read(4));
    const auto frictionMode=read(8),restitutionMode=read(12);
    if(frictionMode>3||restitutionMode>3){error="Prepared physics material contains an unknown combine mode";return false;}
    data.FrictionCombine=static_cast<GameSproutRuntime::PhysicsMaterialCombine>(frictionMode);
    data.RestitutionCombine=static_cast<GameSproutRuntime::PhysicsMaterialCombine>(restitutionMode);
    if(!Valid(data,error))return false;
    output=data;return true;
}
}
