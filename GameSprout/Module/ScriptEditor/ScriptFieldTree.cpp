#include "ScriptFieldTree.h"
#include "ScriptEditor.h"
#include <algorithm>
#include <cmath>

namespace {
std::size_t NodeCost(const GameSproutRuntime::ScriptValue& value,unsigned depth=0){
    if(depth>32)return 10001;
    std::size_t cost=1;
    const auto add=[&](const auto& child){const auto count=NodeCost(child,depth+1);cost=count>10000-cost?10001:cost+count;};
    for(const auto& [id,child]:value.Fields){if(cost>10000)break;add(child);}
    for(const auto& child:value.Elements){if(cost>10000)break;add(child);}
    for(const auto& [key,child]:value.Entries){if(cost>10000)break;add(key);if(cost<=10000)add(child);}
    return cost;
}
}

bool ScriptFieldTree::Resolve(GameSproutRuntime::ScriptValue& root,const GameSproutRuntime::ScriptFieldInfo& schema,
    const ScriptFieldPath& path,GameSproutRuntime::ScriptValue*& result,const GameSproutRuntime::ScriptFieldInfo*& definition,std::string& error) {
    using GameSproutRuntime::ScriptValueKind;
    result=&root;definition=&schema;
    if(path.size()>32){error="字段层级超过限制。";return false;}
    for(const auto& step:path) {
        if(step.Kind==ScriptPathKind::Member&&result->Kind==ScriptValueKind::Structure) {
            const auto value=result->Fields.find(step.Member);
            const auto field=std::find_if(definition->Children.begin(),definition->Children.end(),[&](const auto& child){return child.ID==step.Member;});
            if(value==result->Fields.end()||field==definition->Children.end()) {error="结构成员已经变更。";return false;}
            result=&value->second;definition=&*field;
        }else if(step.Kind==ScriptPathKind::Element&&result->Kind==ScriptValueKind::Sequence&&
                 definition->Children.size()==1&&step.Index<result->Elements.size()) {
            result=&result->Elements[step.Index];definition=&definition->Children[0];
        }else if((step.Kind==ScriptPathKind::EntryKey||step.Kind==ScriptPathKind::EntryValue)&&
                 result->Kind==ScriptValueKind::Dictionary&&definition->Children.size()==2&&step.Index<result->Entries.size()) {
            const bool key=step.Kind==ScriptPathKind::EntryKey;
            result=key?&result->Entries[step.Index].first:&result->Entries[step.Index].second;definition=&definition->Children[key?0:1];
        }else {error="容器内容已经改变，请重新编辑当前属性。";return false;}
    }
    error.clear();return true;
}

bool ScriptFieldTree::EditContainer(GameSproutRuntime::ScriptValue& value,const GameSproutRuntime::ScriptFieldInfo& schema,
    const ScriptContainerEdit& edit,std::string& error) {
    using GameSproutRuntime::ScriptValueKind;
    const bool sequence=value.Kind==ScriptValueKind::Sequence,dictionary=value.Kind==ScriptValueKind::Dictionary;
    if(!sequence&&!dictionary){error="此字段不是容器。";return false;}
    if((sequence&&schema.Children.size()!=1)||(dictionary&&schema.Children.size()!=2)){error="容器字段定义不完整。";return false;}
    const std::size_t size=sequence?value.Elements.size():value.Entries.size();
    if(sequence&&schema.FixedLength>=0&&edit.Action!=ScriptContainerAction::Move){error="固定数组不能修改长度。";return false;}
    if(edit.Action==ScriptContainerAction::Resize) {
        if(!sequence||edit.Index>10000){error="容器长度必须在 0 到 10000 之间。";return false;}
        if(edit.Index>size){const auto unit=NodeCost(schema.Children[0].Default),current=NodeCost(value);
            if(current>10000||unit>10000||(edit.Index-size)>(10000-current)/unit){error="扩容后的嵌套元素总数超过 10000。";return false;}}
        value.Elements.resize(edit.Index,schema.Children[0].Default);
    }else if(edit.Action==ScriptContainerAction::Insert) {
        if(edit.Index>size||size>=10000){error="插入位置无效或已达到容器长度限制。";return false;}
        const auto current=NodeCost(value),added=sequence?NodeCost(schema.Children[0].Default):NodeCost(schema.Children[0].Default)+NodeCost(schema.Children[1].Default);
        if(current>10000||added>10000-current){error="添加后的嵌套元素总数超过 10000。";return false;}
        if(sequence)value.Elements.insert(value.Elements.begin()+edit.Index,schema.Children[0].Default);
        else {
            auto key=schema.Children[0].Default;
            // No silent replacement of an existing key. The user edits the new
            // entry explicitly; choose an unused ordinary key when possible.
            const auto used=[&](const auto& key){return std::any_of(value.Entries.begin(),value.Entries.end(),[&](const auto& entry){return entry.first==key;});};
            if(used(key)) {
                if(!schema.Children[0].EnumValues.empty()){
                    for(const auto& option:schema.Children[0].EnumValues){
                        if(key.Kind==ScriptValueKind::Unsigned)key.Unsigned=option.UnsignedValue;else key.Integer=option.Value;
                        if(!used(key))break;
                    }
                }else if(key.Kind==ScriptValueKind::String){const auto stem=key.String;for(std::size_t index=1;used(key)&&index<=10000;++index)key.String=stem+std::to_string(index);}
                else if(key.Kind==ScriptValueKind::Integer){key.Integer=0;while(used(key)&&key.Integer<10000)++key.Integer;}
                else if(key.Kind==ScriptValueKind::Unsigned){key.Unsigned=0;while(used(key)&&key.Unsigned<10000)++key.Unsigned;}
                else if(key.Kind==ScriptValueKind::Boolean)key.Boolean=!key.Boolean;
            }
            if(used(key)){error="没有可用的默认键，请先修改或删除已有项。";return false;}
            value.Entries.insert(value.Entries.begin()+edit.Index,{key,schema.Children[1].Default});
        }
    }else if(edit.Action==ScriptContainerAction::Remove) {
        if(edit.Index>=size){error="待删除的容器项已经不存在。";return false;}
        if(sequence)value.Elements.erase(value.Elements.begin()+edit.Index);
        else value.Entries.erase(value.Entries.begin()+edit.Index);
    }else {
        if(edit.Index>=size||edit.Destination>=size){error="移动位置已经失效。";return false;}
        const auto move=[&](auto& items){auto item=std::move(items[edit.Index]);items.erase(items.begin()+edit.Index);items.insert(items.begin()+edit.Destination,std::move(item));};
        if(sequence)move(value.Elements);else move(value.Entries);
    }
    error.clear();return true;
}

bool ScriptEditor::SetFieldPath(const ScriptFieldTarget& target,const std::string& id,const ScriptFieldPath& path,
    const GameSproutRuntime::ScriptValue& value,std::string& error) {
    if(path.empty())return SetField(target,id,value,error);
    ScriptPropertySnapshot snapshot;
    if(!ReadFields(target.ObjectID,target.ScriptInstanceID,snapshot,error)||snapshot.Target!=target){if(error.empty())error="字段会话已失效。";return false;}
    for(auto& field:snapshot.Fields)if(field.Info.ID==id) {
        GameSproutRuntime::ScriptValue* leaf=nullptr;const GameSproutRuntime::ScriptFieldInfo* schema=nullptr;
        if(!ScriptFieldTree::Resolve(field.Value,field.Info,path,leaf,schema,error))return false;
        if(leaf->Kind!=value.Kind){error="字段值类型已经改变。";return false;}
        *leaf=value;return SetField(target,id,field.Value,error);
    }
    error="脚本字段已经移除。";return false;
}

bool ScriptEditor::EditContainer(const ScriptFieldTarget& target,const std::string& id,const ScriptFieldPath& path,
    const ScriptContainerEdit& edit,std::string& error) {
    ScriptPropertySnapshot snapshot;
    if(!ReadFields(target.ObjectID,target.ScriptInstanceID,snapshot,error)||snapshot.Target!=target){if(error.empty())error="字段会话已失效。";return false;}
    for(auto& field:snapshot.Fields)if(field.Info.ID==id) {
        GameSproutRuntime::ScriptValue* leaf=nullptr;const GameSproutRuntime::ScriptFieldInfo* schema=nullptr;
        if(!ScriptFieldTree::Resolve(field.Value,field.Info,path,leaf,schema,error)||!ScriptFieldTree::EditContainer(*leaf,*schema,edit,error))return false;
        return SetField(target,id,field.Value,error);
    }
    error="脚本字段已经移除。";return false;
}
