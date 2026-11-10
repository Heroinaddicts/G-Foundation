#include "SceneManager.h"
#include "../Shared/AssetArtifactIO.h"
#include <set>

namespace {
void CollectTags(const Json::Value& value,std::set<std::uint64_t>& output) {
    if(value.isObject()) {
        for(const char* name:{"TagID","RootTagID"})if(value[name].isUInt64())output.insert(value[name].asUInt64());
        const auto& path=value["Path"];
        if(path.isArray()&&!path.empty()&&path[path.size()-1].isString()&&
           (path[path.size()-1]=="TagID"||path[path.size()-1]=="RootTagID"))
            for(const char* name:{"Before","After","Value"})if(value[name].isUInt64())output.insert(value[name].asUInt64());
        for(const auto& name:value.getMemberNames())CollectTags(value[name],output);
    } else if(value.isArray())for(const auto& item:value)CollectTags(item,output);
}
}
bool SceneManager::SetObjectLayer(uint64_t objectID,std::uint8_t layer,bool includeChildren,std::string& error) {
    if(!Ready(error))return false;
    if(layer>=32){error="Layer 编号必须在 0..31。";return false;}
    auto* object=Runtime()->QueryGameObject(objectID);
    if(!object||object==GetActiveScene()||object->GetScene()!=GetActiveScene()) {error="Layer 编辑需要当前文档中的普通节点。";return false;}
    if(!includeChildren) {
        const auto previous=object->GetLayer();if(previous==layer)return true;
        const auto change=[](std::uint8_t value){return [value](GameSproutRuntime::IGameObject& target,std::string& reason){
            if(target.SetLayer(value))return true;reason="Runtime 拒绝修改 Layer。";return false;
        };};
        return EditObject(objectID,"修改对象 Layer",change(layer),change(previous),error);
    }
    SceneSubtreeDocument captured;
    if(!CaptureObjectDocument(objectID,captured,error))return false;
    Json::Value document;
    if(!AssetArtifactIO::Parse(captured.DocumentJson,document)){error="无法读取子树文档。";return false;}
    bool changed=false;
    for(auto& node:document["Objects"]) {
        changed=changed||node.get("Layer",0).asUInt()!=layer;node["Layer"]=Json::UInt(layer);
    }
    if(!changed)return true;
    SceneDocumentEditRequest request;request.RootObjectID=objectID;request.Label="修改对象及子节点 Layer";
    request.DocumentJson=AssetArtifactIO::JsonText(document);SceneSubtreeDocument output;
    return EditObjectDocument(request,output,error);
}
std::vector<std::uint64_t> SceneManager::GetReferencedTagIDs() const {
    std::set<std::uint64_t> tags;
    const auto history=[&](const std::vector<Edit>& entries){
        for(const auto& entry:entries) {
            tags.insert(entry.ReferencedTags.begin(),entry.ReferencedTags.end());
            for(const auto* value:{&entry.Subtree,&entry.DocumentBefore,&entry.DocumentAfter,&entry.AnnotationsBefore,&entry.AnnotationsAfter})CollectTags(*value,tags);
        }
    };
    history(_History);CollectTags(_Annotations,tags);
    if(_DocumentSession){history(_DocumentSession->Main.History);CollectTags(_DocumentSession->Main.Annotations,tags);}
    if(_PlaySession) {
        history(_PlaySession->Author.History);CollectTags(_PlaySession->Author.Annotations,tags);
        tags.insert(_PlaySession->SceneTags.begin(),_PlaySession->SceneTags.end());
    }
    return {tags.begin(),tags.end()};
}
