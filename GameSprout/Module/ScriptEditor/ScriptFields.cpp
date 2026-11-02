#include "ScriptEditor.h"
#include <algorithm>
#include <limits>

namespace {
GameSproutRuntime::IScriptComponent* Script(GameSproutRuntime::IGameObject& object,uint64_t instance) {
    for(std::size_t index=0;index<object.GetComponentCount();++index)
        if(auto* script=dynamic_cast<GameSproutRuntime::IScriptComponent*>(object.GetComponentAt(index));
           script&&script->GetScriptInstanceID()==instance)return script;
    return nullptr;
}
std::string ObjectPath(GameSproutRuntime::IGameObject& object) {
    std::vector<std::string> names;
    for(auto* current=&object;current;current=current->Parent())names.push_back(current->GetName());
    std::string path;
    for(auto current=names.rbegin();current!=names.rend();++current) {
        if(!path.empty())path+='/';
        path+=*current;
    }
    return path;
}
}

GameSproutRuntime::IScriptComponent* ScriptEditor::ResolveFieldTarget(const ScriptFieldTarget& target,std::string& error) const {
    error.clear();
    auto* scene=_Scenes?_Scenes->GetActiveScene():nullptr;
    if(!_Launched||!_Runtime||!scene||target.SessionID!=_FieldSessionID||target.WorldID!=scene->GetInstanceID()) {
        error="脚本字段所属会话已失效，请重新选择对象。";return nullptr;
    }
    auto* object=_Runtime->QueryGameObject(target.ObjectID);
    if(!object||object->GetScene()!=scene) {error="脚本所属对象已经删除或不属于当前场景。";return nullptr;}
    auto* script=Script(*object,target.ScriptInstanceID);
    if(!script)error="脚本组件已经移除。";
    return script;
}

bool ScriptEditor::ReadFields(uint64_t objectID,uint64_t instanceID,ScriptPropertySnapshot& snapshot,std::string& error) const {
    auto* scene=_Scenes?_Scenes->GetActiveScene():nullptr;
    ScriptPropertySnapshot candidate;
    candidate.Target={_FieldSessionID,scene?scene->GetInstanceID():0,objectID,instanceID};
    auto* script=ResolveFieldTarget(candidate.Target,error);
    if(!script)return false;
    GameSproutRuntime::ScriptTypeInfo definition;
    bool found=false;
    for(const auto& type:_Types)if(type.Type.TypeID==script->GetScriptTypeID()) {definition=type.Type;found=true;break;}
    if(!found)for(const auto& [id,value]:script->GetFields())definition.Fields.push_back({id,id,value});
    for(const auto& info:definition.Fields) {
        const auto field=script->GetFields().find(info.ID);
        ScriptFieldSnapshot item;
        item.Info=info;item.Value=field==script->GetFields().end()?info.Default:field->second;
        if(item.Value.Kind==GameSproutRuntime::ScriptValueKind::SceneObject&&item.Value.SceneObject.Assigned) {
            auto* target=_Runtime->QuerySceneObjectByFileID(scene,item.Value.SceneObject.ObjectFileID);
            item.MissingObject=!target;
            if(target) {item.ObjectName=target->GetName();item.ObjectPath=ObjectPath(*target);}
        }
        candidate.Fields.push_back(std::move(item));
    }
    snapshot=std::move(candidate);error.clear();return true;
}

bool ScriptEditor::ObserveFields(const ScriptFieldTarget& target,IScriptEditorListener* listener) {
    if(!listener)return false;
    std::string error;
    if(!ResolveFieldTarget(target,error))return false;
    for(const auto& [id,current]:_Listeners)if(current==listener) {
        _FieldObservers.insert_or_assign(listener,target);return true;
    }
    return false;
}
void ScriptEditor::UnobserveFields(IScriptEditorListener* listener) {_FieldObservers.erase(listener);}

bool ScriptEditor::SetField(const ScriptFieldTarget& target,const std::string& fieldID,
                            const GameSproutRuntime::ScriptValue& value,std::string& error) {
    auto* script=ResolveFieldTarget(target,error);
    if(!script)return false;
    if(!script->IsScriptTypeAvailable()) {error="脚本类型尚未编译，不能修改字段。";return false;}
    const auto field=script->GetFields().find(fieldID);
    if(field==script->GetFields().end()||field->second.Kind!=value.Kind) {error="脚本字段已经变更或值类型不一致。";return false;}
    const auto before=field->second;
    if(before==value)return true;
    if(value.Kind==GameSproutRuntime::ScriptValueKind::SceneObject) {
        if(!value.SceneObject.Assigned&&value.SceneObject.ObjectFileID) {error="空引用不能带有对象 ID。";return false;}
        if(value.SceneObject.Assigned&&!_Runtime->QuerySceneObjectByFileID(_Scenes->GetActiveScene(),value.SceneObject.ObjectFileID)) {
            error="引用目标已经删除或不属于当前场景。";return false;
        }
    }
    if(_Scenes->IsPlaySessionActive()) {
        if(_Runtime->IsExecuting()) {error="请在当前 Runtime 回调结束后修改脚本字段。";return false;}
        if(!script->SetField(fieldID,value,error))return false;
        NotifyFields();return true;
    }
    const auto operation=[instance=target.ScriptInstanceID,fieldID](const GameSproutRuntime::ScriptValue& data) {
        return [instance,fieldID,data](GameSproutRuntime::IGameObject& object,std::string& failure) {
            auto* component=Script(object,instance);
            if(!component) {failure="脚本组件已经移除。";return false;}
            return component->SetField(fieldID,data,failure);
        };
    };
    if(!_Scenes->EditObject(target.ObjectID,"修改脚本属性",operation(value),operation(before),error))return false;
    // SceneManager's completed transaction event also covers Undo/Redo.
    return true;
}

bool ScriptEditor::GetSceneObjectChoices(const ScriptFieldTarget& target,
                                        std::vector<ScriptObjectChoice>& choices,std::string& error) const {
    if(!ResolveFieldTarget(target,error))return false;
    auto* scene=_Scenes->GetActiveScene();
    std::vector<GameSproutRuntime::IGameObject*> pending{scene};
    std::vector<ScriptObjectChoice> candidate;
    for(std::size_t index=0;index<pending.size();++index) {
        auto* object=pending[index];uint64_t fileID=0;
        if(_Runtime->GetSceneObjectFileID(object,fileID))candidate.push_back({fileID,object->GetName(),ObjectPath(*object)});
        for(std::size_t child=0;child<object->GetChildCount();++child)
            if(auto* node=object->GetChild(child))pending.push_back(node);
    }
    choices.swap(candidate);error.clear();return true;
}

bool ScriptEditor::ReadObjectTarget(uint64_t objectID,ScriptObjectTarget& target,std::string& error) const {
    error.clear();
    auto* scene=_Scenes?_Scenes->GetActiveScene():nullptr;
    auto* object=_Runtime?_Runtime->QueryGameObject(objectID):nullptr;
    if(!_Launched||!scene||!object||object->GetScene()!=scene) {
        error="对象已经删除或不属于当前场景。";return false;
    }
    target={_FieldSessionID,scene->GetInstanceID(),objectID};return true;
}

bool ScriptEditor::ReferenceFromObject(const ScriptFieldTarget& target,const ScriptObjectTarget& source,
                                       GameSproutRuntime::ScriptValue& value,std::string& error) const {
    if(!ResolveFieldTarget(target,error))return false;
    if(source.SessionID!=_FieldSessionID||source.WorldID!=target.WorldID) {
        error="拖入对象所属会话已经失效或不属于当前场景。";return false;
    }
    auto* scene=_Scenes->GetActiveScene();
    auto* object=_Runtime->QueryGameObject(source.ObjectID);
    if(!object||object->GetScene()!=scene) {error="只能引用当前场景中的对象。";return false;}
    uint64_t fileID=0;
    if(!_Runtime->GetSceneObjectFileID(object,fileID)) {error="该运行时对象没有可保存的场景身份。";return false;}
    GameSproutRuntime::ScriptValue candidate;
    candidate.Kind=GameSproutRuntime::ScriptValueKind::SceneObject;
    candidate.SceneObject={true,fileID};
    value=std::move(candidate);error.clear();return true;
}

bool ScriptEditor::LocateObjectReference(const ScriptFieldTarget& target,const GameSproutRuntime::ScriptValue& value,std::string& error) {
    if(!ResolveFieldTarget(target,error))return false;
    if(!_Selection) {error="节点选择服务不可用。";return false;}
    if(value.Kind!=GameSproutRuntime::ScriptValueKind::SceneObject||!value.SceneObject.Assigned) {error="尚未设置对象引用。";return false;}
    auto* object=_Runtime->QuerySceneObjectByFileID(_Scenes->GetActiveScene(),value.SceneObject.ObjectFileID);
    if(!object) {error="引用目标已经删除。";return false;}
    _Selection->SetSceneObjectSelection({target.WorldID,object->GetInstanceID()});error.clear();return true;
}

void ScriptEditor::NotifyFields() {
    auto* scene=_Scenes?_Scenes->GetActiveScene():nullptr;
    if(!scene)return;
    const auto watched=_FieldObservers;
    for(const auto& [listener,target]:watched) {
        const auto found=_FieldObservers.find(listener);
        if(found!=_FieldObservers.end()&&found->second==target)listener->OnScriptFieldsChanged(target);
    }
}
void ScriptEditor::OnActiveSceneChanged() {
    ++_FieldSessionID;
    if(!_FieldSessionID)++_FieldSessionID;
    _FieldObservers.clear();
}
void ScriptEditor::OnSceneStateChanged() {NotifyFields();}
void ScriptEditor::OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event) {
    auto* scene=_Scenes?_Scenes->GetActiveScene():nullptr;
    if(!scene||event.SceneID!=scene->GetInstanceID())return;
    using GameSproutRuntime::SceneNodeEventType;
    switch(event.Type) {
    case SceneNodeEventType::Create:case SceneNodeEventType::Destroy:
    case SceneNodeEventType::Rename:case SceneNodeEventType::Reparent:
        NotifyFields();break;
    case SceneNodeEventType::ComponentAdded:case SceneNodeEventType::ComponentRemoved:case SceneNodeEventType::ComponentChanged:
        if(event.ComponentType=="GameSproutRuntime.ScriptComponent")NotifyFields();
        break;
    default:break;
    }
}
