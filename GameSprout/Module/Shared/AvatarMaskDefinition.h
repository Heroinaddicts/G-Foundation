#pragma once
#include "AnimationDefinition.h"

namespace AvatarMaskDefinition {
// Paths are source-Skeleton identities, never editor object names or runtime IDs.
inline bool Path(const std::string& path) {
    if(path.empty()||path.size()>4096||path.front()!='/'||path.find('\\')!=std::string::npos
        ||path.find('\0')!=std::string::npos||path.find("//")!=std::string::npos)return false;
    std::size_t offset=1;
    while(offset<path.size()) {
        const auto end=path.find('/',offset);const auto part=path.substr(offset,end-offset);
        if(part=="."||part==".."||part.empty())return false;
        if(end==std::string::npos)break;offset=end+1;
    }
    return path=="/"||path.back()!='/';
}
inline bool Read(const Json::Value& source,bool prepared,GameSproutRuntime::AvatarMaskData& output,std::string& error) {
    output={};error.clear();
    const auto fail=[&error](const std::string& message){error=message;return false;};
    if(!AnimationDefinition::Keys(source,{"Version","Format","Mode","Skeleton","DefaultWeight","RootMotionWeight","Nodes","HumanParts"},error))return false;
    if(source["Version"]!=1||(!prepared&&source.isMember("Format"))||(prepared&&source["Format"]!="AvatarMask2"))return fail("Unsupported AvatarMask format");
    GameSproutRuntime::AvatarMaskData data;
    if(source.isMember("Mode")) {
        if(source["Mode"]=="Transform")data.Mode=GameSproutRuntime::AvatarMaskMode::Transform;
        else if(source["Mode"]=="Humanoid")data.Mode=GameSproutRuntime::AvatarMaskMode::Humanoid;
        else return fail("Unsupported AvatarMask Mode");
    } else if(prepared)return fail("Prepared AvatarMask requires Mode");
    if(data.Mode==GameSproutRuntime::AvatarMaskMode::Transform) {
        if(!AnimationDefinition::Reference(source["Skeleton"],data.Skeleton))return fail("Transform AvatarMask requires a Skeleton reference");
    } else if(source.isMember("Skeleton"))return fail("Humanoid mask cannot reference a concrete Skeleton");
    const auto weight=[&](const Json::Value& value,float& destination) {
        double number;if(!AnimationDefinition::Number(value,number)||number<0||number>1)return false;
        destination=static_cast<float>(number);return true;
    };
    if((prepared||source.isMember("DefaultWeight"))&&!weight(source["DefaultWeight"],data.DefaultWeight))return fail("DefaultWeight must be in [0,1]");
    if((prepared||source.isMember("RootMotionWeight"))&&!weight(source["RootMotionWeight"],data.RootMotionWeight))return fail("RootMotionWeight must be in [0,1]");
    if((prepared||source.isMember("Nodes"))&&(!source["Nodes"].isArray()||source["Nodes"].size()>100000))return fail("AvatarMask Nodes must be an array");
    if(source.isMember("HumanParts")&&(!source["HumanParts"].isArray()||source["HumanParts"].size()>static_cast<unsigned>(GameSproutRuntime::HumanBodyPart::Count)))return fail("Invalid HumanParts array");
    std::set<unsigned> parts;
    for(const auto& value:source["HumanParts"]) {
        if(!AnimationDefinition::Keys(value,{"Part","Weight"},error))return false;
        GameSproutRuntime::HumanBodyPartWeight entry;bool found=false;
        for(unsigned index=0;index<static_cast<unsigned>(GameSproutRuntime::HumanBodyPart::Count);++index)
            if(value["Part"]==GameSproutRuntime::HumanBodyPartName(static_cast<GameSproutRuntime::HumanBodyPart>(index))){entry.Part=static_cast<GameSproutRuntime::HumanBodyPart>(index);found=true;break;}
        if(!found||!parts.insert(static_cast<unsigned>(entry.Part)).second||!weight(value["Weight"],entry.Weight))return fail("Human mask body parts must be unique and use weights in [0,1]");
        data.HumanParts.push_back(entry);
    }
    if(data.Mode==GameSproutRuntime::AvatarMaskMode::Transform&&!data.HumanParts.empty())return fail("Transform mask cannot contain Humanoid body parts");
    if(data.Mode==GameSproutRuntime::AvatarMaskMode::Humanoid&&!source["Nodes"].empty())return fail("Humanoid mask cannot contain Skeleton paths");
    std::set<std::string> paths;
    for(const auto& value:source["Nodes"]) {
        GameSproutRuntime::AvatarMaskNode node;
        if(!AnimationDefinition::Keys(value,{"Path","Weight"},error))return false;
        if(!value["Path"].isString()||!Path(value["Path"].asString())||!paths.insert(value["Path"].asString()).second
            ||!weight(value["Weight"],node.Weight))return fail("Mask nodes require unique source paths and weights in [0,1]");
        node.Path=value["Path"].asString();data.Nodes.push_back(std::move(node));
    }
    output=std::move(data);return true;
}
inline Json::Value Write(const GameSproutRuntime::AvatarMaskData& data,bool prepared) {
    Json::Value source;source["Version"]=1;if(prepared)source["Format"]="AvatarMask2";
    source["Mode"]=data.Mode==GameSproutRuntime::AvatarMaskMode::Transform?"Transform":data.Mode==GameSproutRuntime::AvatarMaskMode::Humanoid?"Humanoid":"Invalid";
    if(data.Mode==GameSproutRuntime::AvatarMaskMode::Transform)source["Skeleton"]=AnimationDefinition::Reference(data.Skeleton);
    source["HumanParts"]=Json::Value(Json::arrayValue);
    for(const auto& part:data.HumanParts){Json::Value entry;entry["Part"]=GameSproutRuntime::HumanBodyPartName(part.Part);entry["Weight"]=part.Weight;source["HumanParts"].append(entry);}
    source["DefaultWeight"]=data.DefaultWeight;
    source["RootMotionWeight"]=data.RootMotionWeight;source["Nodes"]=Json::Value(Json::arrayValue);
    for(const auto& node:data.Nodes){Json::Value value;value["Path"]=node.Path;value["Weight"]=node.Weight;source["Nodes"].append(value);}
    return source;
}
// Validate against the actual prepared Skeleton, including disconnected roots and
// non-topological storage. Defaults inherit through Parent, not string prefixes.
inline bool Validate(const GameSproutRuntime::AvatarMaskData& data,const GameSproutRuntime::SkeletonData& skeleton,std::string& error) {
    if(!std::isfinite(data.DefaultWeight)||data.DefaultWeight<0||data.DefaultWeight>1||!std::isfinite(data.RootMotionWeight)||data.RootMotionWeight<0||data.RootMotionWeight>1){error="Invalid AvatarMask weights";return false;}
    if(data.Mode==GameSproutRuntime::AvatarMaskMode::Humanoid) {
        if(!data.Skeleton.Guid.empty()||data.Skeleton.LocalID||!data.Nodes.empty()){error="Humanoid mask cannot bind concrete Skeleton paths";return false;}
        std::set<unsigned> parts;for(const auto& part:data.HumanParts)if(part.Part>=GameSproutRuntime::HumanBodyPart::Count||!parts.insert(static_cast<unsigned>(part.Part)).second||!std::isfinite(part.Weight)||part.Weight<0||part.Weight>1){error="Invalid Humanoid body part mask";return false;}
        return true;
    }
    if(data.Mode!=GameSproutRuntime::AvatarMaskMode::Transform||!data.HumanParts.empty()){error="Invalid Transform mask mode";return false;}
    if(skeleton.Nodes.empty()||skeleton.Nodes.size()>100000){error="Mask Skeleton is empty or exceeds supported limits";return false;}
    std::set<std::string> paths;
    for(const auto& node:skeleton.Nodes) {
        if(!Path(node.Path)||!paths.insert(node.Path).second||node.Parent<-1||node.Parent>=static_cast<int>(skeleton.Nodes.size())) {
            error="Invalid Mask Skeleton path or parent";return false;
        }
    }
    std::vector<unsigned char> marks(skeleton.Nodes.size());
    for(std::size_t index=0;index<skeleton.Nodes.size();++index) {
        int cursor=static_cast<int>(index);
        while(cursor>=0&&marks[cursor]==0){marks[cursor]=1;cursor=skeleton.Nodes[cursor].Parent;}
        if(cursor>=0&&marks[cursor]==1){error="Mask Skeleton contains a parent cycle";return false;}
        cursor=static_cast<int>(index);while(cursor>=0&&marks[cursor]==1){marks[cursor]=2;cursor=skeleton.Nodes[cursor].Parent;}
    }
    for(const auto& node:data.Nodes)if(!paths.count(node.Path)){error="Mask path does not exist in Skeleton: "+node.Path;return false;}
    return true;
}
}
