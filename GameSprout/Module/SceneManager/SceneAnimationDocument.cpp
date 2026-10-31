#include "SceneAnimationDocument.h"
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
[[noreturn]] void Reject(const std::string& path, const std::string& message) {
    throw std::runtime_error(path + ": " + message);
}
void Fields(const Json::Value& value, const std::string& path, std::initializer_list<const char*> fields) {
    if (!value.isObject()) Reject(path,"expected object");
    for (const auto& key:value.getMemberNames()) {
        bool found=false;
        for (const char* field:fields) if (key==field) {found=true;break;}
        if (!found) Reject(path+"."+key,"unknown field");
    }
}
std::uint64_t ID(const Json::Value& value, const std::string& path) {
    if ((!value.isInt64()&&!value.isUInt64())||!value.isUInt64()) Reject(path,"expected unsigned integer");
    return value.asUInt64();
}
GameSproutRuntime::ResourceReference Reference(const Json::Value& value, const std::string& path) {
    if (value.isNull()) return {};
    Fields(value,path,{"Guid","LocalID"});
    if (!value["Guid"].isString()) Reject(path,"expected Guid string");
    const std::string guid=value["Guid"].asString();
    bool nonzero=false;
    if (guid.size()!=32) Reject(path,"expected 32 hexadecimal GUID digits");
    for (char digit:guid) {
        if (!((digit>='0'&&digit<='9')||(digit>='a'&&digit<='f'))) Reject(path,"expected lowercase hexadecimal GUID");
        nonzero=nonzero||digit!='0';
    }
    const auto local=ID(value["LocalID"],path+".LocalID");
    if (!nonzero||!local) Reject(path,"resource identity must be nonzero");
    return {guid,local};
}
Json::Value Reference(const GameSproutRuntime::ResourceReference& reference) {
    if (reference.Guid.empty()) {
        if (reference.LocalID) Reject("Animation","invalid unbound resource");
        return {};
    }
    Json::Value result(Json::objectValue);
    result["Guid"]=reference.Guid;result["LocalID"]=Json::UInt64(reference.LocalID);
    Reference(result,"Animation resource");return result;
}
std::uint64_t FileID(std::uint64_t runtimeID, const SceneDocument::ObjectIDs& ids) {
    const auto found=ids.RuntimeToFile.find(runtimeID);
    if (found==ids.RuntimeToFile.end()) Reject("Animation binding","target is not a serializable scene node");
    return found->second;
}
GameSproutRuntime::RootMotionMode MotionMode(const Json::Value& value, const std::string& path) {
    if (!value.isMember("RootMotionMode")) return GameSproutRuntime::RootMotionMode::Disabled;
    if (!value["RootMotionMode"].isString()) Reject(path+".RootMotionMode","expected mode string");
    const std::string mode=value["RootMotionMode"].asString();
    if (mode=="Disabled") return GameSproutRuntime::RootMotionMode::Disabled;
    if (mode=="ExtractOnly") return GameSproutRuntime::RootMotionMode::ExtractOnly;
    if (mode=="ApplyToObject") return GameSproutRuntime::RootMotionMode::ApplyToObject;
    Reject(path+".RootMotionMode","unknown root motion mode");
}
const char* MotionModeName(GameSproutRuntime::RootMotionMode mode) {
    switch (mode) {
    case GameSproutRuntime::RootMotionMode::Disabled:return "Disabled";
    case GameSproutRuntime::RootMotionMode::ExtractOnly:return "ExtractOnly";
    case GameSproutRuntime::RootMotionMode::ApplyToObject:return "ApplyToObject";
    }
    Reject("Animator.RootMotionMode","invalid mode");
}
const Json::Value* FindComponent(const Json::Value& node, const char* type) {
    for (const auto& component:node["Components"]) if (component["Type"]==type) return &component;
    return nullptr;
}
}

namespace SceneAnimationDocument {
bool Handles(const std::string& type) {
    return type=="GameSproutRuntime.Animator"||type=="GameSproutRuntime.SkinnedMeshRenderer";
}
void Validate(const Json::Value& data, const std::string& path) {
    const std::string type=data["Type"].asString();
    if (type=="GameSproutRuntime.SkinnedMeshRenderer") {
        Fields(data,path,{"Type","AnimatorObjectFileID"});
        ID(data["AnimatorObjectFileID"],path+".AnimatorObjectFileID");return;
    }
    Fields(data,path,{"Type","Skeleton","Avatar","Controller","DefaultClip","Speed","PlayOnStart","Loop","Bindings","RootMotionMode"});
    MotionMode(data,path);
    Reference(data["Skeleton"],path+".Skeleton");Reference(data["Controller"],path+".Controller");
    Reference(data["Avatar"],path+".Avatar");
    Reference(data["DefaultClip"],path+".DefaultClip");
    if (!data["Speed"].isNumeric()||!std::isfinite(data["Speed"].asDouble())||data["Speed"].asDouble()<0||
        data["Speed"].asDouble()>std::numeric_limits<float>::max()) Reject(path+".Speed","expected finite nonnegative float");
    if (!data["PlayOnStart"].isBool()||!data["Loop"].isBool()) Reject(path,"expected playback configuration booleans");
    const auto& bindings=data["Bindings"];
    if (!bindings.isArray()||bindings.size()>100000) Reject(path+".Bindings","expected bounded binding array");
    std::set<std::string> paths;
    std::set<std::uint64_t> objects;
    for (const auto& binding:bindings) {
        Fields(binding,path+".Bindings",{"Path","ObjectFileID"});
        if (!binding["Path"].isString()||binding["Path"].asString().empty()||
            binding["Path"].asString().size()>65536||binding["Path"].asString().find('\0')!=std::string::npos||
            !paths.insert(binding["Path"].asString()).second) Reject(path+".Bindings","invalid or duplicate node path");
        const auto id=ID(binding["ObjectFileID"],path+".Bindings.ObjectFileID");
        if (id&&!objects.insert(id).second) Reject(path+".Bindings","invalid or duplicate binding object");
    }
}
void Create(const Json::Value& data, GameSproutRuntime::IGameObject& object) {
    if (data["Type"]=="GameSproutRuntime.SkinnedMeshRenderer") {
        if (!object.AddComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>()) Reject("Scene","cannot create SkinnedMeshRenderer");
        return;
    }
    auto* animator=object.AddComponent<GameSproutRuntime::IAnimatorComponent>();
    if (!animator) Reject("Scene","cannot create Animator");
    animator->SetSkeleton(Reference(data["Skeleton"],"Animator.Skeleton"));
    animator->SetAvatar(Reference(data["Avatar"],"Animator.Avatar"));
    animator->SetController(Reference(data["Controller"],"Animator.Controller"));
    animator->SetDefaultClip(Reference(data["DefaultClip"],"Animator.DefaultClip"));
    if (!animator->SetSpeed(data["Speed"].asFloat())) Reject("Animator.Speed","Runtime rejected value");
    animator->SetPlayOnStart(data["PlayOnStart"].asBool());animator->SetLoop(data["Loop"].asBool());
    if(!animator->SetRootMotionMode(MotionMode(data,"Animator"))) Reject("Animator.RootMotionMode","Runtime rejected mode");
}
Json::Value Serialize(GameSproutRuntime::IComponent& component, const SceneDocument::ObjectIDs& ids) {
    Json::Value result(Json::objectValue);result["Type"]=component.GetTypeName();
    if (auto* skin=dynamic_cast<GameSproutRuntime::ISkinnedMeshRendererComponent*>(&component)) {
        result["AnimatorObjectFileID"]=Json::UInt64(skin->GetAnimatorObjectID()?FileID(skin->GetAnimatorObjectID(),ids):0);
        return result;
    }
    auto* animator=dynamic_cast<GameSproutRuntime::IAnimatorComponent*>(&component);
    if (!animator) Reject("Scene","Animator interface mismatch");
    for (const auto& field:std::vector<std::pair<const char*,GameSproutRuntime::ResourceReference>>{
             {"Skeleton",animator->GetSkeleton()},{"Avatar",animator->GetAvatar()},
             {"Controller",animator->GetController()},{"DefaultClip",animator->GetDefaultClip()}}) {
        auto reference=Reference(field.second);if (!reference.isNull()) result[field.first]=std::move(reference);
    }
    result["RootMotionMode"]=MotionModeName(animator->GetRootMotionMode());
    result["Speed"]=animator->GetSpeed();result["PlayOnStart"]=animator->GetPlayOnStart();result["Loop"]=animator->GetLoop();
    result["Bindings"]=Json::Value(Json::arrayValue);
    for (const auto& binding:animator->GetNodeBindings()) {
        Json::Value value(Json::objectValue);value["Path"]=binding.Path;
        value["ObjectFileID"]=Json::UInt64(binding.ObjectID?FileID(binding.ObjectID,ids):0);result["Bindings"].append(std::move(value));
    }
    Validate(result,"Animator");return result;
}
void ValidateReferences(const Json::Value& document) {
    std::map<std::uint64_t,const Json::Value*> nodes;
    for (const auto& node:document["Objects"]) nodes.emplace(node["ID"].asUInt64(),&node);
    const auto validate=[&](const Json::Value& components) {
        for (const auto& component:components) {
            if (component["Type"]=="GameSproutRuntime.Animator") {
                for (const auto& binding:component["Bindings"])
                    if (binding["ObjectFileID"].asUInt64()&&!nodes.count(binding["ObjectFileID"].asUInt64())) Reject("Animator.Bindings","missing target node");
            } else if (component["Type"]=="GameSproutRuntime.SkinnedMeshRenderer") {
                const auto id=component["AnimatorObjectFileID"].asUInt64();
                if (id&&(!nodes.count(id)||!FindComponent(*nodes.at(id),"GameSproutRuntime.Animator")))
                    Reject("SkinnedMeshRenderer","target must contain an Animator");
            }
        }
    };
    validate(document["RootComponents"]);
    for (const auto& node:document["Objects"]) validate(node["Components"]);
}
Json::Value CaptureBindings(const Json::Value& document) {
    Json::Value result(Json::arrayValue);
    const auto capture=[&](std::uint64_t id,const Json::Value& components) {
        for(const auto& component:components) if(Handles(component["Type"].asString())) {
            Json::Value item(Json::objectValue);item["Owner"]=Json::UInt64(id);
            item["Data"]=component;result.append(std::move(item));
        }
    };
    capture(0,document["RootComponents"]);
    for(const auto& node:document["Objects"])capture(node["ID"].asUInt64(),node["Components"]);
    return result;
}
void RestoreBindings(const Json::Value& snapshot,GameSproutRuntime::IGameSproutRuntime& runtime,
    GameSproutRuntime::IScene& scene,const SceneDocument::ObjectIDs& ids) {
    std::map<std::uint64_t,GameSproutRuntime::IGameObject*> objects{{0,&scene}};
    for(const auto& entry:ids.RuntimeToFile)if(auto* object=runtime.QueryGameObject(entry.first))objects.emplace(entry.second,object);
    for(const auto& item:snapshot) {
        const auto found=objects.find(item["Owner"].asUInt64());if(found==objects.end())continue;
        const auto& data=item["Data"];
        if(data["Type"]=="GameSproutRuntime.Animator") {
            auto* animator=found->second->GetComponent<GameSproutRuntime::IAnimatorComponent>();if(!animator)continue;
            std::vector<GameSproutRuntime::AnimationNodeBinding> bindings;
            for(const auto& binding:data["Bindings"]) {
                const auto targetID=binding["ObjectFileID"].asUInt64();
                const auto target=objects.find(targetID);
                bindings.push_back({binding["Path"].asString(),!targetID||target==objects.end()?0:target->second->GetInstanceID()});
            }
            if(!animator->SetNodeBindings(bindings))Reject("Undo animation binding","Runtime rejected restored bindings");
        }
    }
    for(const auto& item:snapshot)if(item["Data"]["Type"]=="GameSproutRuntime.SkinnedMeshRenderer") {
        const auto owner=objects.find(item["Owner"].asUInt64());if(owner==objects.end())continue;
        const auto targetID=item["Data"]["AnimatorObjectFileID"].asUInt64();
        const auto target=objects.find(targetID);
        if(auto* skin=owner->second->GetComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>())
            if(!skin->SetAnimatorObjectID(targetID&&target!=objects.end()?target->second->GetInstanceID():0))Reject("Undo skin binding","Runtime rejected restored Animator");
    }
}
void Bind(const Json::Value& document, GameSproutRuntime::IScene& scene,
    const std::map<std::uint64_t,GameSproutRuntime::IGameObject*>& objects) {
    const auto bind=[&](const Json::Value& components,GameSproutRuntime::IGameObject* object) {
        for (const auto& component:components) {
            if (component["Type"]=="GameSproutRuntime.Animator") {
                std::vector<GameSproutRuntime::AnimationNodeBinding> bindings;
                for (const auto& binding:component["Bindings"]) bindings.push_back({binding["Path"].asString(),(binding["ObjectFileID"].asUInt64()?objects.at(binding["ObjectFileID"].asUInt64())->GetInstanceID():0)});
                if (!object->GetComponent<GameSproutRuntime::IAnimatorComponent>()->SetNodeBindings(bindings))
                    Reject("Animator.Bindings","Runtime rejected node mapping");
            }
        }
    };
    bind(document["RootComponents"],&scene);
    for (const auto& node:document["Objects"]) bind(node["Components"],objects.at(node["ID"].asUInt64()));
    for (const auto& node:document["Objects"]) for (const auto& component:node["Components"])
        if (component["Type"]=="GameSproutRuntime.SkinnedMeshRenderer") {
            const auto id=component["AnimatorObjectFileID"].asUInt64();
            if (!objects.at(node["ID"].asUInt64())->GetComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>()->SetAnimatorObjectID(id?objects.at(id)->GetInstanceID():0))
                Reject("SkinnedMeshRenderer","Runtime rejected Animator binding");
        }
}
}
