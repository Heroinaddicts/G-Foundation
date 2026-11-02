#include "SceneManager.h"
#include "../Shared/AssetArtifactIO.h"

bool SceneManager::SetObjectStaticFlags(uint64_t objectID,GameSproutRuntime::StaticFlags values,
    GameSproutRuntime::StaticFlags changedFlags,bool includeChildren,std::string& error) {
    if(!Ready(error))return false;
    if(!GameSproutRuntime::IsValidStaticFlags(values)||!GameSproutRuntime::IsValidStaticFlags(changedFlags)) {
        error="StaticFlags 包含未知标志。";return false;
    }
    auto* object=Runtime()->QueryGameObject(objectID);
    if(!object||object==GetActiveScene()||object->GetScene()!=GetActiveScene()) {error="静态标志编辑需要当前文档中的普通节点。";return false;}
    const auto mask=static_cast<std::uint32_t>(changedFlags),desired=static_cast<std::uint32_t>(values)&mask;
    if(!mask)return true;
    if(!includeChildren) {
        const auto previous=object->GetStaticFlags();
        const auto next=static_cast<GameSproutRuntime::StaticFlags>((static_cast<std::uint32_t>(previous)&~mask)|desired);
        if(previous==next)return true;
        const auto change=[](GameSproutRuntime::StaticFlags value){return [value](GameSproutRuntime::IGameObject& target,std::string& reason){
            if(target.SetStaticFlags(value))return true;reason="Runtime 拒绝修改静态标志。";return false;
        };};
        return EditObject(objectID,"修改对象静态标志",change(next),change(previous),error);
    }
    SceneSubtreeDocument captured;if(!CaptureObjectDocument(objectID,captured,error))return false;
    Json::Value document;
    if(!AssetArtifactIO::Parse(captured.DocumentJson,document)){error="无法读取子树文档。";return false;}
    bool changed=false;
    for(auto& node:document["Objects"]) {
        const auto old=node.get("StaticFlags",0).asUInt(),next=(old&~mask)|desired;
        changed=changed||old!=next;node["StaticFlags"]=Json::UInt(next);
    }
    if(!changed)return true;
    SceneDocumentEditRequest request;request.RootObjectID=objectID;request.Label="修改对象及子节点静态标志";
    request.DocumentJson=AssetArtifactIO::JsonText(document);SceneSubtreeDocument output;
    return EditObjectDocument(request,output,error);
}
