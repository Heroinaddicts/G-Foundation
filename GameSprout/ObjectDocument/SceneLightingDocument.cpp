#include "SceneLightingDocument.h"
#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
[[noreturn]] void Fail(const std::string& path,const char* reason) {throw std::runtime_error(path+": "+reason);}
void Keys(const Json::Value& value,const std::string& path,std::initializer_list<const char*> allowed) {
    if(!value.isObject())Fail(path,"expected object");
    for(const auto& key:value.getMemberNames()) {
        bool found=false;for(const char* member:allowed)if(key==member){found=true;break;}
        if(!found)Fail(path+"."+key,"unknown field");
    }
}
float Number(const Json::Value& value,const std::string& path) {
    if(!value.isNumeric()||!std::isfinite(value.asDouble())||value.asDouble()<0||
       value.asDouble()>std::numeric_limits<float>::max())Fail(path,"expected nonnegative finite float");
    return value.asFloat();
}

constexpr std::array StaticUpdateModes{
    std::pair{GameSproutRuntime::StaticShadowUpdateMode::AutomaticIncremental,"AutomaticIncremental"},
    std::pair{GameSproutRuntime::StaticShadowUpdateMode::FullCascade,"FullCascade"}
};
constexpr std::array FilterQualities{
    std::pair{GameSproutRuntime::ShadowFilterQuality::Low,"Low"},
    std::pair{GameSproutRuntime::ShadowFilterQuality::Medium,"Medium"},
    std::pair{GameSproutRuntime::ShadowFilterQuality::High,"High"}
};
template<class Enum,std::size_t Size>
Enum ReadChoice(const Json::Value& value,const std::string& path,const std::array<std::pair<Enum,const char*>,Size>& choices) {
    if(value.isString()) {
        const auto name=value.asString();
        for(const auto& [candidate,label]:choices)if(name==label)return candidate;
    }
    Fail(path,"invalid shadow option");
}
template<class Enum,std::size_t Size>
const char* ChoiceName(Enum value,const std::array<std::pair<Enum,const char*>,Size>& choices) {
    for(const auto& [candidate,label]:choices)if(value==candidate)return label;
    Fail("Light.Settings.Shadows","invalid Runtime shadow option");
}

GameSproutRuntime::LightSettings Read(const Json::Value& component,const std::string& path) {
    Keys(component,path,{"Type","Settings","ID"});
    if(!component["Type"].isString()||!SceneLightingDocument::Handles(component["Type"].asString()))Fail(path+".Type","unknown light component");
    GameSproutRuntime::LightSettings result;
    if(!component.isMember("Settings"))return result;
    const auto& settings=component["Settings"];const std::string key=path+".Settings";
    Keys(settings,key,{"Type","Enabled","Color","Intensity","CullingMask","Shadows"});
    if(settings.isMember("Type")&&(!settings["Type"].isString()||settings["Type"].asString()!="Directional"))
        Fail(key+".Type","only Directional is supported");
    if(settings.isMember("Enabled")) {
        if(!settings["Enabled"].isBool())Fail(key+".Enabled","expected bool");
        result.Enabled=settings["Enabled"].asBool();
    }
    if(settings.isMember("Color")) {
        const auto& color=settings["Color"];
        if(!color.isArray()||color.size()!=4)Fail(key+".Color","expected four linear RGBA numbers");
        result.Color={Number(color[0],key+".Color[0]"),Number(color[1],key+".Color[1]"),
            Number(color[2],key+".Color[2]"),Number(color[3],key+".Color[3]")};
        if(result.Color.A>1)Fail(key+".Color[3]","alpha must be between zero and one");
    }
    if(settings.isMember("Intensity"))result.Intensity=Number(settings["Intensity"],key+".Intensity");
    if(settings.isMember("CullingMask")) {
        const auto& mask=settings["CullingMask"];
        if((mask.type()!=Json::intValue&&mask.type()!=Json::uintValue)||!mask.isUInt64()||
           mask.asUInt64()>std::numeric_limits<std::uint32_t>::max())Fail(key+".CullingMask","expected uint32");
        result.CullingMask=static_cast<std::uint32_t>(mask.asUInt64());
    }
    if(settings.isMember("Shadows")) {
        const auto& shadows=settings["Shadows"];const std::string shadowKey=key+".Shadows";
        Keys(shadows,shadowKey,{"Enabled","Strength","Distance","Resolution","CascadeCount","DepthBias","NormalBias",
            "Soft","CacheStaticCasters","StaticUpdateMode","CascadeSplits","FadeStart","FilterQuality"});
        for(const auto& [name,target]:{std::pair<const char*,bool*>{"Enabled",&result.Shadows.Enabled},
            {"Soft",&result.Shadows.Soft},{"CacheStaticCasters",&result.Shadows.CacheStaticCasters}})
            if(shadows.isMember(name)) {
                if(!shadows[name].isBool())Fail(shadowKey+"."+name,"expected bool");
                *target=shadows[name].asBool();
            }
        for(const auto& [name,target]:{std::pair<const char*,float*>{"Strength",&result.Shadows.Strength},
            {"Distance",&result.Shadows.Distance},{"DepthBias",&result.Shadows.DepthBias},
            {"NormalBias",&result.Shadows.NormalBias},{"FadeStart",&result.Shadows.FadeStart}})
            if(shadows.isMember(name))*target=Number(shadows[name],shadowKey+"."+name);
        for(const auto& [name,target]:{std::pair<const char*,std::uint32_t*>{"Resolution",&result.Shadows.Resolution},{"CascadeCount",&result.Shadows.CascadeCount}})
            if(shadows.isMember(name)) {
                if(!shadows[name].isUInt())Fail(shadowKey+"."+name,"expected uint32");
                *target=shadows[name].asUInt();
            }
        if(result.Shadows.FadeStart>1)Fail(shadowKey+".FadeStart","expected value in [0,1]");
        if(shadows.isMember("StaticUpdateMode"))
            result.Shadows.StaticUpdateMode=ReadChoice(shadows["StaticUpdateMode"],shadowKey+".StaticUpdateMode",StaticUpdateModes);
        if(shadows.isMember("FilterQuality"))
            result.Shadows.FilterQuality=ReadChoice(shadows["FilterQuality"],shadowKey+".FilterQuality",FilterQualities);
        if(shadows.isMember("CascadeSplits")) {
            const auto& splits=shadows["CascadeSplits"];
            const auto splitKey=shadowKey+".CascadeSplits";
            if(!splits.isArray()||splits.size()>result.Shadows.CascadeSplits.size()||
                (splits.size()!=0&&splits.size()+1!=result.Shadows.CascadeCount))
                Fail(splitKey,"expected an empty array or one split per internal cascade boundary");
            float previous=0;
            for(Json::ArrayIndex index=0;index<splits.size();++index) {
                const auto entry=splitKey+"["+std::to_string(index)+"]";
                const float split=Number(splits[index],entry);
                if(split<=previous||split>=1)Fail(entry,"expected a strictly increasing split in (0,1)");
                result.Shadows.CascadeSplits[index]=split;
                previous=split;
            }
        }
        if(!GameSproutRuntime::IsValidLightShadows(result.Shadows))
            Fail(shadowKey,"invalid shadow settings: strength [0,1], positive distance, resolution 256/512/1024/2048/4096, cascades 1/2/4, biases [0,10] texels");
    }
    return result;
}
}

namespace SceneLightingDocument {
bool Handles(const std::string& type) {return type=="GameSproutRuntime.Light";}
void Validate(const Json::Value& component,const std::string& path) {(void)Read(component,path);}
GameSproutRuntime::IComponent* Create(const Json::Value& component,GameSproutRuntime::IGameObject& object) {
    const auto settings=Read(component,"Light");
    auto* result=object.AddComponent<GameSproutRuntime::ILightComponent>();
    if(!result||!result->SetSettings(settings))Fail("Light","Runtime rejected light settings");
    return result;
}
Json::Value Serialize(GameSproutRuntime::IComponent& component) {
    const auto* light=dynamic_cast<GameSproutRuntime::ILightComponent*>(&component);
    if(!light||!component.GetTypeName()||!Handles(component.GetTypeName()))Fail("Light","interface mismatch");
    const auto& settings=light->GetSettings();
    if(!GameSproutRuntime::IsValidLightShadows(settings.Shadows))Fail("Light.Settings.Shadows","invalid Runtime settings");
    Json::Value result(Json::objectValue);result["Type"]="GameSproutRuntime.Light";
    auto& data=result["Settings"];data["Type"]="Directional";data["Enabled"]=settings.Enabled;
    auto& color=data["Color"];color=Json::Value(Json::arrayValue);
    color.append(settings.Color.R);color.append(settings.Color.G);color.append(settings.Color.B);color.append(settings.Color.A);
    data["Intensity"]=settings.Intensity;data["CullingMask"]=Json::UInt(settings.CullingMask);
    auto& shadows=data["Shadows"];
    shadows["Enabled"]=settings.Shadows.Enabled;shadows["Strength"]=settings.Shadows.Strength;
    shadows["Distance"]=settings.Shadows.Distance;shadows["Resolution"]=Json::UInt(settings.Shadows.Resolution);
    shadows["CascadeCount"]=Json::UInt(settings.Shadows.CascadeCount);shadows["DepthBias"]=settings.Shadows.DepthBias;
    shadows["NormalBias"]=settings.Shadows.NormalBias;shadows["Soft"]=settings.Shadows.Soft;
    shadows["CacheStaticCasters"]=settings.Shadows.CacheStaticCasters;
    shadows["StaticUpdateMode"]=ChoiceName(settings.Shadows.StaticUpdateMode,StaticUpdateModes);
    shadows["FadeStart"]=settings.Shadows.FadeStart;
    shadows["FilterQuality"]=ChoiceName(settings.Shadows.FilterQuality,FilterQualities);
    auto& splits=shadows["CascadeSplits"];splits=Json::Value(Json::arrayValue);
    if(settings.Shadows.CascadeSplits[0]>0)
        for(std::uint32_t index=0;index+1<settings.Shadows.CascadeCount;++index)
            splits.append(settings.Shadows.CascadeSplits[index]);
    Validate(result,"Light");
    return result;
}
}
