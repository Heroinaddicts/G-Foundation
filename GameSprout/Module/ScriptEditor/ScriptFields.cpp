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
GameSproutRuntime::IGameObject* ReferencedObject(GameSproutRuntime::IGameSproutRuntime& runtime,
    GameSproutRuntime::IScene& scene,const GameSproutRuntime::ScriptObjectReference& reference) {
    if(!reference.Assigned)return nullptr;
    if(reference.RuntimeSceneID||reference.RuntimeObjectID) {
        if(reference.ObjectFileID||reference.RuntimeSceneID!=scene.GetInstanceID()||!reference.RuntimeObjectID)return nullptr;
        auto* object=runtime.QueryGameObject(reference.RuntimeObjectID);
        return object&&object->GetScene()==&scene?object:nullptr;
    }
    return runtime.QuerySceneObjectByFileID(&scene,reference.ObjectFileID);
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
    std::map<std::string,GameSproutRuntime::ScriptValue> fields;
    if(!script->ReadFields(fields,error))return false;
    GameSproutRuntime::ScriptTypeInfo definition;
    bool found=false;
    for(const auto& type:_Types)if(type.Type.TypeID==script->GetScriptTypeID()) {definition=type.Type;found=true;break;}
    if(!found)for(const auto& [id,value]:fields)definition.Fields.push_back({id,id,value});
    for(const auto& info:definition.Fields) {
        const auto field=fields.find(info.ID);
        candidate.Fields.push_back(DescribeField(info,field==fields.end()?info.Default:field->second));
    }
    snapshot=std::move(candidate);error.clear();return true;
}

bool ScriptEditor::ObserveFields(const ScriptFieldTarget& target,IScriptEditorListener* listener) {
    if(!listener)return false;
    std::string error;
    if(!ResolveFieldTarget(target,error))return false;
    for(const auto& [id,current]:_Listeners)if(current==listener) {
        _FieldObservers.insert_or_assign(listener,target);RefreshFieldObservation();
        if(_ObservedObjects.contains(target.ObjectID))return true;
        _FieldObservers.erase(listener);return false;
    }
    return false;
}
void ScriptEditor::UnobserveFields(IScriptEditorListener* listener) {_FieldObservers.erase(listener);RefreshFieldObservation();}

void ScriptEditor::RefreshFieldObservation() {
    std::set<std::uint64_t> wanted;
    for(const auto& [listener,target]:_FieldObservers)wanted.insert(target.ObjectID);
    for(const auto id:_ObservedObjects)if(!wanted.contains(id))_Runtime->UnregisterObjectPropertyObserver(id,this);
    for(auto current=wanted.begin();current!=wanted.end();) {
        if(!_ObservedObjects.contains(*current)&&!_Runtime->RegisterObjectPropertyObserver(*current,this))current=wanted.erase(current);
        else ++current;
    }
    _ObservedObjects=std::move(wanted);
}

bool ScriptEditor::SetField(const ScriptFieldTarget& target,const std::string& fieldID,
                            const GameSproutRuntime::ScriptValue& value,std::string& error) {
    if(!_Editing||_NotifyingFields) {error="属性编辑服务不可用，或正在分发字段通知。";return false;}
    if(!_Editing->CanEdit({target.WorldID,target.ObjectID,0},error))return false;
    auto* script=ResolveFieldTarget(target,error);
    if(!script)return false;
    if(!script->IsScriptTypeAvailable()) {error="脚本类型尚未编译，不能修改字段。";return false;}
    std::map<std::string,GameSproutRuntime::ScriptValue> fields;
    if(!script->ReadFields(fields,error))return false;
    const auto field=fields.find(fieldID);
    if(field==fields.end()||field->second.Kind!=value.Kind) {error="脚本字段已经变更或值类型不一致。";return false;}
    const auto before=field->second;
    if(before==value)return true;
    const GameSproutRuntime::ScriptFieldInfo* schema=nullptr;
    for(const auto& type:_Types)if(type.Type.TypeID==script->GetScriptTypeID())
        for(const auto& definition:type.Type.Fields)if(definition.ID==fieldID)schema=&definition;
    if(!schema){error="脚本字段定义已经变更。";return false;}
    if(!ValidateFieldValue(*schema,value,error,0,&before))return false;
    const auto operation=[instance=target.ScriptInstanceID,fieldID](const GameSproutRuntime::ScriptValue& data) {
        return [instance,fieldID,data](GameSproutRuntime::IGameObject& object,std::string& failure) {
            auto* component=Script(object,instance);
            if(!component) {failure="脚本组件已经移除。";return false;}
            return component->SetField(fieldID,data,failure);
        };
    };
    if(!_Editing->EditObject({target.WorldID,target.ObjectID,script->GetComponentInstanceID()},"修改脚本属性",operation(value),operation(before),error))return false;
    if(_Scenes->IsPlaySessionActive())NotifyFields(target.ObjectID);
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
        const bool saved=_Runtime->GetSceneObjectFileID(object,fileID);
        if(saved||_Scenes->IsPlaySessionActive())candidate.push_back({fileID,object->GetName(),ObjectPath(*object),
            saved?0:object->GetInstanceID(),saved?0:scene->GetInstanceID()});
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
    GameSproutRuntime::ScriptValue candidate;
    candidate.Kind=GameSproutRuntime::ScriptValueKind::SceneObject;
    if(_Runtime->GetSceneObjectFileID(object,fileID))candidate.SceneObject={true,fileID};
    else {
        if(!_Scenes->IsPlaySessionActive()) {error="该对象没有可保存的场景身份。";return false;}
        if(!_Editing||!_Editing->CanEdit({target.WorldID,target.ObjectID,0},error))return false;
        candidate.SceneObject={true,0,scene->GetInstanceID(),object->GetInstanceID()};
    }
    value=std::move(candidate);error.clear();return true;
}

bool ScriptEditor::LocateObjectReference(const ScriptFieldTarget& target,const GameSproutRuntime::ScriptValue& value,std::string& error) {
    if(!ResolveFieldTarget(target,error))return false;
    if(!_Selection) {error="节点选择服务不可用。";return false;}
    if(value.Kind!=GameSproutRuntime::ScriptValueKind::SceneObject||!value.SceneObject.Assigned) {error="尚未设置对象引用。";return false;}
    auto* object=ReferencedObject(*_Runtime,*_Scenes->GetActiveScene(),value.SceneObject);
    if(!object) {error="引用目标已经删除。";return false;}
    _Selection->SetSceneObjectSelection({target.WorldID,object->GetInstanceID()});error.clear();return true;
}

void ScriptEditor::NotifyFields(uint64_t objectID) {
    auto* scene=_Scenes?_Scenes->GetActiveScene():nullptr;
    if(!scene||_NotifyingFields||_FieldObservers.empty())return;
    _NotifyingFields=true;struct Reset{bool& Value;~Reset(){Value=false;}} reset{_NotifyingFields};
    const auto watched=_FieldObservers;
    for(const auto& [listener,target]:watched) {
        if(objectID&&target.ObjectID!=objectID)continue;
        const auto found=_FieldObservers.find(listener);
        if(found!=_FieldObservers.end()&&found->second==target)try{listener->OnScriptFieldsChanged(target);}catch(...){}
    }
}
void ScriptEditor::OnActiveSceneChanged() {
    for(const auto id:_ObservedObjects)_Runtime->UnregisterObjectPropertyObserver(id,this);
    _ObservedObjects.clear();
    ++_FieldSessionID;
    if(!_FieldSessionID)++_FieldSessionID;
    _FieldObservers.clear();
}
void ScriptEditor::OnRuntimeObjectPropertiesChanged(uint64_t sceneID,uint64_t objectID,
    GameSproutRuntime::RuntimePropertyChange changes) {
    if(!GameSproutRuntime::HasRuntimePropertyChange(changes,GameSproutRuntime::RuntimePropertyChange::ScriptFields))return;
    auto* scene=_Scenes?_Scenes->GetActiveScene():nullptr;
    if(scene&&scene->GetInstanceID()==sceneID&&_ObservedObjects.contains(objectID))NotifyFields(objectID);
}
void ScriptEditor::OnSceneStateChanged() {NotifyFields();}
void ScriptEditor::OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event) {
    auto* scene=_Scenes?_Scenes->GetActiveScene():nullptr;
    if(!scene||event.SceneID!=scene->GetInstanceID())return;
    using GameSproutRuntime::SceneNodeEventType;
    switch(event.Type) {
    case SceneNodeEventType::Create:case SceneNodeEventType::Destroy:
    case SceneNodeEventType::Rename:case SceneNodeEventType::Reparent:
    case SceneNodeEventType::ComponentAdded:case SceneNodeEventType::ComponentRemoved:
        // Reference paths and resolution may change for fields on other objects.
        NotifyFields();
        break;
    case SceneNodeEventType::ComponentChanged:
        if(event.ComponentType=="GameSproutRuntime.ScriptComponent")NotifyFields(event.ObjectID);
        break;
    default:break;
    }
}
