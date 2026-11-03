#include "SceneEnvironmentLightingDocument.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
[[noreturn]] void Fail(const std::string& path,const char* reason){throw std::runtime_error(path+": "+reason);}
float Number(const Json::Value& value,const std::string& path,bool signedValue=false) {
    if(!value.isNumeric()||!std::isfinite(value.asDouble())||
       std::fabs(value.asDouble())>std::numeric_limits<float>::max()||(!signedValue&&value.asDouble()<0))
        Fail(path,signedValue?"expected finite float":"expected nonnegative finite float");
    return value.asFloat();
}
GameSproutRuntime::Color Color(const Json::Value& data,const std::string& path) {
    if(!data.isArray()||data.size()!=4)Fail(path,"expected four linear RGBA values");
    GameSproutRuntime::Color color;
    color.R=Number(data[0],path+"[0]");color.G=Number(data[1],path+"[1]");
    color.B=Number(data[2],path+"[2]");color.A=Number(data[3],path+"[3]");
    if(color.A>1)Fail(path+"[3]","alpha must be between zero and one");
    return color;
}
Json::Value Color(GameSproutRuntime::Color color) {
    Json::Value data(Json::arrayValue);data.append(color.R);data.append(color.G);data.append(color.B);data.append(color.A);return data;
}
}
namespace SceneEnvironmentLightingDocument {
GameSproutRuntime::EnvironmentLightingSettings Read(const Json::Value& data,const std::string& path) {
    if(!data.isObject())Fail(path,"expected object");
    for(const auto& name:data.getMemberNames())
        if(name!="Mode"&&name!="AmbientColor"&&name!="SkyColor"&&name!="EquatorColor"&&name!="GroundColor"&&name!="Intensity"&&name!="SHCoefficients"&&name!="Atmosphere")
            Fail(path+"."+name,"unknown field");
    GameSproutRuntime::EnvironmentLightingSettings result;
    if(data.isMember("Mode")) {
        if(!data["Mode"].isString())Fail(path+".Mode","expected ambient mode string");
        const auto mode=data["Mode"].asString();
        if(mode=="None")result.Mode=GameSproutRuntime::AmbientLightMode::None;
        else if(mode=="Color")result.Mode=GameSproutRuntime::AmbientLightMode::Color;
        else if(mode=="Gradient")result.Mode=GameSproutRuntime::AmbientLightMode::Gradient;
        else if(mode=="SphericalHarmonics")result.Mode=GameSproutRuntime::AmbientLightMode::SphericalHarmonics;
        else Fail(path+".Mode","unknown ambient mode");
    }
    for(const auto& entry:{std::pair{"AmbientColor",&result.AmbientColor},std::pair{"SkyColor",&result.SkyColor},
        std::pair{"EquatorColor",&result.EquatorColor},std::pair{"GroundColor",&result.GroundColor}})
        if(data.isMember(entry.first))*entry.second=Color(data[entry.first],path+"."+entry.first);
    if(data.isMember("Intensity"))result.Intensity=Number(data["Intensity"],path+".Intensity");
    if(data.isMember("SHCoefficients")) {
        const auto& coefficients=data["SHCoefficients"];
        if(!coefficients.isArray()||coefficients.size()!=9)Fail(path+".SHCoefficients","expected nine RGB coefficients");
        for(Json::ArrayIndex index=0;index<9;++index) {
            const auto& coefficient=coefficients[index];const auto key=path+".SHCoefficients["+std::to_string(index)+"]";
            if(!coefficient.isArray()||coefficient.size()!=3)Fail(key,"expected three signed RGB values");
            result.SHCoefficients[index]={Number(coefficient[0],key+"[0]",true),Number(coefficient[1],key+"[1]",true),Number(coefficient[2],key+"[2]",true)};
        }
    }
    if(data.isMember("Atmosphere")) {
        const auto& atmosphere=data["Atmosphere"];const auto key=path+".Atmosphere";
        if(!atmosphere.isObject())Fail(key,"expected object");
        for(const auto& name:atmosphere.getMemberNames())
            if(name!="Enabled"&&name!="DrawSky"&&name!="ZenithColor"&&name!="HorizonColor"&&name!="FogColor"&&
               name!="FogStart"&&name!="FogEnd"&&name!="FogMaxOpacity")Fail(key+"."+name,"unknown field");
        auto& settings=result.Atmosphere;
        for(const auto& entry:{std::pair{"Enabled",&settings.Enabled},std::pair{"DrawSky",&settings.DrawSky}})
            if(atmosphere.isMember(entry.first)) {
                if(!atmosphere[entry.first].isBool())Fail(key+"."+entry.first,"expected boolean");
                *entry.second=atmosphere[entry.first].asBool();
            }
        for(const auto& entry:{std::pair{"ZenithColor",&settings.ZenithColor},std::pair{"HorizonColor",&settings.HorizonColor},
            std::pair{"FogColor",&settings.FogColor}})
            if(atmosphere.isMember(entry.first))*entry.second=Color(atmosphere[entry.first],key+"."+entry.first);
        for(const auto& entry:{std::pair{"FogStart",&settings.FogStart},std::pair{"FogEnd",&settings.FogEnd},
            std::pair{"FogMaxOpacity",&settings.FogMaxOpacity}})
            if(atmosphere.isMember(entry.first))*entry.second=Number(atmosphere[entry.first],key+"."+entry.first);
        if(settings.FogEnd<=settings.FogStart)Fail(key+".FogEnd","must be greater than FogStart");
        if(settings.FogMaxOpacity>1)Fail(key+".FogMaxOpacity","must be between zero and one");
    }
    return result;
}
Json::Value Serialize(const GameSproutRuntime::EnvironmentLightingSettings& settings) {
    if(!GameSproutRuntime::IsValidEnvironmentLighting(settings))Fail("Scene.EnvironmentLighting","invalid Runtime environment lighting");
    Json::Value result(Json::objectValue);
    switch(settings.Mode) {
    case GameSproutRuntime::AmbientLightMode::None:result["Mode"]="None";break;
    case GameSproutRuntime::AmbientLightMode::Color:result["Mode"]="Color";break;
    case GameSproutRuntime::AmbientLightMode::Gradient:result["Mode"]="Gradient";break;
    case GameSproutRuntime::AmbientLightMode::SphericalHarmonics:result["Mode"]="SphericalHarmonics";break;
    }
    result["AmbientColor"]=Color(settings.AmbientColor);result["SkyColor"]=Color(settings.SkyColor);
    result["EquatorColor"]=Color(settings.EquatorColor);result["GroundColor"]=Color(settings.GroundColor);
    result["Intensity"]=settings.Intensity;
    result["SHCoefficients"]=Json::Value(Json::arrayValue);
    for(const auto& coefficient:settings.SHCoefficients) {
        Json::Value value(Json::arrayValue);value.append(coefficient.X);value.append(coefficient.Y);value.append(coefficient.Z);
        result["SHCoefficients"].append(std::move(value));
    }
    const auto& atmosphere=settings.Atmosphere;auto& data=result["Atmosphere"];
    data["Enabled"]=atmosphere.Enabled;data["DrawSky"]=atmosphere.DrawSky;
    data["ZenithColor"]=Color(atmosphere.ZenithColor);data["HorizonColor"]=Color(atmosphere.HorizonColor);
    data["FogColor"]=Color(atmosphere.FogColor);data["FogStart"]=atmosphere.FogStart;
    data["FogEnd"]=atmosphere.FogEnd;data["FogMaxOpacity"]=atmosphere.FogMaxOpacity;
    return result;
}
}
