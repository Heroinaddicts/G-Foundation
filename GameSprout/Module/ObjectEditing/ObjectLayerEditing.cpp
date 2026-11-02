#include "ObjectEditing.h"
#include <vector>

bool ObjectEditing::SetLayer(const ObjectEditTarget& target,std::uint8_t layer,bool includeChildren,std::string& error) {
    if(!CanEdit(target,error))return false;
    if(layer>=32){error="Layer 编号必须在 0..31。";return false;}
    auto* root=Resolve(target);
    if(root==_Scenes->GetActiveScene()){error="场景根不提供对象 Layer 编辑。";return false;}
    if(!_Scenes->IsPlaySessionActive()) {
        _Busy=true;struct Reset{bool& Value;~Reset(){Value=false;}} reset{_Busy};
        const bool accepted=_Scenes->SetObjectLayer(target.ObjectID,layer,includeChildren,error);
        if(accepted)Notify(target,false);
        return accepted;
    }
    struct Item{std::uint64_t ID;std::uint8_t Before;};
    std::vector<Item> values;
    std::vector<GameSproutRuntime::IGameObject*> pending{root};
    while(!pending.empty()) {
        auto* object=pending.back();pending.pop_back();
        values.push_back({object->GetInstanceID(),object->GetLayer()});
        if(includeChildren)for(std::size_t index=0;index<object->GetChildCount();++index)pending.push_back(object->GetChild(index));
    }
    // All candidates are resolved before the first setter. Only runtime identities
    // and values survive into the command; no authoring document is captured.
    const auto change=[runtime=_Runtime,sceneID=target.SceneID,values,layer](bool restore) {
        return [runtime,sceneID,values,layer,restore](GameSproutRuntime::IGameObject&,std::string& failure) {
            for(const auto& item:values) {
                auto* object=runtime->QueryGameObject(item.ID);
                if(!object||!object->GetScene()||object->GetScene()->GetInstanceID()!=sceneID) {failure="子树对象已经失效。";return false;}
            }
            for(const auto& item:values)if(!runtime->QueryGameObject(item.ID)->SetLayer(restore?item.Before:layer)) {
                failure="Runtime 拒绝子树 Layer。";return false;
            }
            return true;
        };
    };
    return EditObject(target,"修改运行对象 Layer",change(false),change(true),error);
}
