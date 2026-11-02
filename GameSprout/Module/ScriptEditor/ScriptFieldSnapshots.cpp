#include "ScriptEditor.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace {
std::string PathOf(GameSproutRuntime::IGameObject& object) {
    std::vector<std::string> names;
    for(auto* current=&object;current;current=current->Parent())names.push_back(current->GetName());
    std::string path;
    for(auto current=names.rbegin();current!=names.rend();++current){if(!path.empty())path+='/';path+=*current;}
    return path;
}
GameSproutRuntime::IGameObject* ObjectOf(GameSproutRuntime::IGameSproutRuntime& runtime,GameSproutRuntime::IScene& scene,
    const GameSproutRuntime::ScriptObjectReference& reference) {
    if(!reference.Assigned)return nullptr;
    if(reference.RuntimeSceneID||reference.RuntimeObjectID){
        if(reference.ObjectFileID||reference.RuntimeSceneID!=scene.GetInstanceID()||!reference.RuntimeObjectID)return nullptr;
        auto* object=runtime.QueryGameObject(reference.RuntimeObjectID);return object&&object->GetScene()==&scene?object:nullptr;
    }
    return runtime.QuerySceneObjectByFileID(&scene,reference.ObjectFileID);
}
}
ScriptFieldSnapshot ScriptEditor::DescribeField(const GameSproutRuntime::ScriptFieldInfo& schema,
    const GameSproutRuntime::ScriptValue& value,unsigned depth) const {
    ScriptFieldSnapshot result;result.Info=schema;result.Value=value;
    if(depth>32)return result;
    using GameSproutRuntime::ScriptValueKind;
    auto* scene=_Scenes?_Scenes->GetActiveScene():nullptr;
    GameSproutRuntime::IGameObject* object=nullptr;
    if(scene&&value.Kind==ScriptValueKind::SceneObject&&value.SceneObject.Assigned){
        object=ObjectOf(*_Runtime,*scene,value.SceneObject);result.MissingObject=!object;
    }else if(scene&&value.Kind==ScriptValueKind::Component&&value.Component.Assigned){
        auto* component=ResolveComponentReference(value.Component);
        if(component&&_Runtime->IsComponentType(component,schema.ComponentType)) {
            object=component->GetGameObject();result.ObjectName=component->GetTypeName();
            for(const auto& type:_Runtime->GetBuiltinComponentTypes())if(type.TypeID==result.ObjectName){result.ObjectName=type.Name;break;}
            if(const auto* script=dynamic_cast<GameSproutRuntime::IScriptComponent*>(component))
                for(const auto& type:_Types)if(type.Type.TypeID==script->GetScriptTypeID()){result.ObjectName=type.Type.Name;break;}
        }else result.MissingObject=true;
    }
    if(object){
        result.ObjectPath=PathOf(*object);
        if(result.ObjectName.empty())result.ObjectName=object->GetName();
        else result.ObjectName=object->GetName()+" ("+result.ObjectName+")";
    }
    if(value.Kind==ScriptValueKind::Structure){
        for(const auto& child:schema.Children){const auto item=value.Fields.find(child.ID);result.Children.push_back(DescribeField(child,item==value.Fields.end()?child.Default:item->second,depth+1));}
    }else if(value.Kind==ScriptValueKind::Sequence&&schema.Children.size()==1){
        for(std::size_t index=0;index<value.Elements.size();++index){auto child=schema.Children[0];child.Name="Element "+std::to_string(index);result.Children.push_back(DescribeField(child,value.Elements[index],depth+1));}
    }else if(value.Kind==ScriptValueKind::Dictionary&&schema.Children.size()==2){
        for(const auto& entry:value.Entries){auto key=schema.Children[0],item=schema.Children[1];key.Name="Key";item.Name="Value";result.Children.push_back(DescribeField(key,entry.first,depth+1));result.Children.push_back(DescribeField(item,entry.second,depth+1));}
    }
    return result;
}

bool ScriptEditor::ValidateFieldValue(const GameSproutRuntime::ScriptFieldInfo& schema,
    const GameSproutRuntime::ScriptValue& value,std::string& error,unsigned depth,const GameSproutRuntime::ScriptValue* previous) const {
    using GameSproutRuntime::ScriptValueKind;
    const auto fail=[&](const char* message){error=message;return false;};
    if(depth>32)return fail("字段层级超过限制。");
    if(value.Kind!=schema.Default.Kind)return fail("字段值与声明类型不一致。");
    // A previously authored missing reference is preserved while editing an
    // unrelated member. Newly assigned references must resolve immediately.
    if(previous&&*previous==value){error.clear();return true;}
    const auto finite=[](auto... numbers){return (std::isfinite(numbers)&&...);};
    switch(value.Kind){
    case ScriptValueKind::Number:if(!finite(value.Number))return fail("数值必须为有限数。");break;
    case ScriptValueKind::String:if(value.String.find('\0')!=std::string::npos)return fail("文本不能包含空字符。");break;
    case ScriptValueKind::Vector2:if(!finite(value.Vector2.X,value.Vector2.Y))return fail("向量必须为有限数。");break;
    case ScriptValueKind::Vector3:if(!finite(value.Vector.X,value.Vector.Y,value.Vector.Z))return fail("向量必须为有限数。");break;
    case ScriptValueKind::Vector4:if(!finite(value.Vector4.X,value.Vector4.Y,value.Vector4.Z,value.Vector4.W))return fail("向量必须为有限数。");break;
    case ScriptValueKind::Quaternion:if(!finite(value.Rotation.X,value.Rotation.Y,value.Rotation.Z,value.Rotation.W))return fail("旋转必须为有限数。");break;
    case ScriptValueKind::Color:if(!finite(value.Tint.R,value.Tint.G,value.Tint.B,value.Tint.A))return fail("颜色必须为有限数。");break;
    case ScriptValueKind::SceneObject:{
        const auto& reference=value.SceneObject;
        if(!reference.Assigned){if(reference.ObjectFileID||reference.RuntimeSceneID||reference.RuntimeObjectID)return fail("空引用不能包含对象身份。");break;}
        if((reference.RuntimeSceneID||reference.RuntimeObjectID)&&!_Scenes->IsPlaySessionActive())return fail("编辑场景不能保存运行时对象引用。");
        if(!ObjectOf(*_Runtime,*_Scenes->GetActiveScene(),reference))return fail("引用对象已经删除或不属于当前场景。");
        break;
    }
    case ScriptValueKind::Component:{
        const auto& reference=value.Component;
        if(!reference.Assigned){if(reference.ComponentFileID||reference.RuntimeSceneID||reference.RuntimeObjectID||reference.RuntimeComponentID)return fail("空组件引用不能包含组件身份。");break;}
        if((reference.RuntimeSceneID||reference.RuntimeObjectID||reference.RuntimeComponentID)&&!_Scenes->IsPlaySessionActive())return fail("编辑场景不能保存运行时组件引用。");
        auto* component=ResolveComponentReference(reference);
        if(!component||!_Runtime->IsComponentType(component,schema.ComponentType))return fail("组件已经删除、不属于当前场景或类型不匹配。");
        break;
    }
    case ScriptValueKind::Structure:
        if(value.Fields.size()!=schema.Children.size())return fail("结构成员与当前声明不一致。");
        for(const auto& child:schema.Children){
            const auto found=value.Fields.find(child.ID);if(found==value.Fields.end())return fail("结构缺少成员。");
            const auto old=previous?previous->Fields.find(child.ID):value.Fields.end();
            const auto* before=previous&&old!=previous->Fields.end()?&old->second:nullptr;
            if(!ValidateFieldValue(child,found->second,error,depth+1,before))return false;
        }
        break;
    case ScriptValueKind::Sequence:
        if(schema.Children.size()!=1||value.Elements.size()>10000)return fail("序列定义无效或长度超过 10000。");
        if(schema.FixedLength>=0&&value.Elements.size()!=static_cast<std::size_t>(schema.FixedLength))return fail("固定数组长度与声明不一致。");
        for(std::size_t index=0;index<value.Elements.size();++index)
            if(!ValidateFieldValue(schema.Children[0],value.Elements[index],error,depth+1,previous&&index<previous->Elements.size()?&previous->Elements[index]:nullptr))return false;
        break;
    case ScriptValueKind::Dictionary:{
        if(schema.Children.size()!=2||value.Entries.size()>10000)return fail("字典定义无效或长度超过 10000。");
        std::set<std::string> keys;
        for(std::size_t index=0;index<value.Entries.size();++index){
            const auto& [key,item]=value.Entries[index];
            if(!ValidateFieldValue(schema.Children[0],key,error,depth+1,previous&&index<previous->Entries.size()?&previous->Entries[index].first:nullptr)||
               !ValidateFieldValue(schema.Children[1],item,error,depth+1,previous&&index<previous->Entries.size()?&previous->Entries[index].second:nullptr))return false;
            std::string identity;
            switch(key.Kind){
            case ScriptValueKind::String:identity=key.String;break;
            case ScriptValueKind::Integer:identity=std::to_string(key.Integer);break;
            case ScriptValueKind::Unsigned:identity=std::to_string(key.Unsigned);break;
            case ScriptValueKind::Boolean:identity=key.Boolean?"1":"0";break;
            default:return fail("字典键必须为整数、布尔、枚举或字符串。");
            }
            if(!keys.insert(identity).second)return fail("字典中已经存在相同的 Key；没有覆盖原值。");
        }
        break;
    }
    default:break;
    }
    error.clear();return true;
}
