#include "PrefabDocument.h"
#include <set>
#include <stdexcept>

namespace PrefabDocument {
bool Validate(const Json::Value& data,std::string& error) {
    if(!SceneDocument::Validate(data,error))return false;
    if(!data["RootComponents"].empty()||data.get("RootMainCamera",false).asBool()) {
        error="Prefab must not contain scene-root components or a scene-root camera";return false;
    }
    std::set<std::uint64_t> ids;unsigned roots=0;
    for(const auto& node:data["Objects"]) {
        ids.insert(node["ID"].asUInt64());
        if(node.get("ParentID",Json::UInt64(0)).asUInt64()==0)++roots;
    }
    if(roots!=1){error="Prefab requires exactly one object root";return false;}
    for(const auto& node:data["Objects"])for(const auto& component:node["Components"])
        if(component["Type"]=="GameSproutRuntime.ScriptComponent")
            for(const auto& field:component["Fields"])if(field["Kind"]=="SceneObject"&&!field["Value"].isNull())
                if(!ids.count(field["Value"]["ObjectFileID"].asUInt64())) {
                    error="Prefab script object reference points outside the template";return false;
                }
    error.clear();return true;
}
bool Encode(const Json::Value& data,std::vector<std::uint8_t>& bytes,std::string& error) {
    if(!Validate(data,error))return false;
    Json::Value wrapper(Json::objectValue);wrapper["Format"]="GameSproutPrefab";wrapper["Data"]=data;
    if(!ObjectDocumentBinary::Encode(wrapper,bytes)){error="Prefab binary encoding failed";return false;}
    return true;
}
bool Decode(const std::vector<std::uint8_t>& bytes,Json::Value& data,std::string& error) {
    Json::Value wrapper;
    if(!ObjectDocumentBinary::Decode(bytes,wrapper)||!wrapper.isObject()||wrapper.size()!=2||
       wrapper["Format"]!="GameSproutPrefab"||!wrapper.isMember("Data")) {
        error="Invalid current GameSproutPrefab BinaryValueLE1 payload";return false;
    }
    if(!Validate(wrapper["Data"],error))return false;
    data=std::move(wrapper["Data"]);return true;
}
bool Remap(Json::Value& data,const std::map<std::uint64_t,std::uint64_t>& ids,std::string& error) {
    try {
        Json::Value result=data;
        const auto remap=[&](Json::Value& value,bool zero=true) {
            const auto source=value.asUInt64();
            if(!source&&zero)return;
            const auto found=ids.find(source);
            if(found==ids.end())throw std::runtime_error("Missing template identity mapping "+std::to_string(source));
            value=Json::UInt64(found->second);
        };
        if(result.isMember("MainCameraID"))remap(result["MainCameraID"]);
        for(auto& node:result["Objects"]) {
            remap(node["ID"],false);if(node.isMember("ParentID"))remap(node["ParentID"]);
            for(auto& component:node["Components"]) {
                if(component.isMember("ID"))remap(component["ID"],false);
                if(component["Type"]=="GameSproutRuntime.Animator")
                    for(auto& binding:component["Bindings"])remap(binding["ObjectFileID"]);
                else if(component["Type"]=="GameSproutRuntime.SkinnedMeshRenderer")remap(component["AnimatorObjectFileID"]);
                else if(component["Type"]=="GameSproutRuntime.ScriptComponent")
                    for(auto& field:component["Fields"])if(field["Kind"]=="SceneObject"&&!field["Value"].isNull())remap(field["Value"]["ObjectFileID"],false);
            }
        }
        data.swap(result);error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
}
