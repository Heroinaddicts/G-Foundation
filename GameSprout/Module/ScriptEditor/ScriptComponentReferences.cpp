#include "ScriptEditor.h"

namespace {
GameSproutRuntime::IComponent* LiveComponent(GameSproutRuntime::IGameObject* object,uint64_t id) {
    if(object)for(std::size_t index=0;index<object->GetComponentCount();++index)
        if(auto* component=object->GetComponentAt(index);component&&component->GetComponentInstanceID()==id)return component;
    return nullptr;
}
std::string ObjectPath(GameSproutRuntime::IGameObject& object) {
    std::vector<std::string> names;for(auto* current=&object;current;current=current->Parent())names.push_back(current->GetName());
    std::string result;for(auto current=names.rbegin();current!=names.rend();++current){if(!result.empty())result+='/';result+=*current;}return result;
}
}
GameSproutRuntime::IComponent* ScriptEditor::ResolveComponentReference(const GameSproutRuntime::ScriptComponentReference& reference) const {
    auto* scene=_Scenes?_Scenes->GetActiveScene():nullptr;
    if(!_Runtime||!scene||!reference.Assigned)return nullptr;
    if(reference.RuntimeSceneID||reference.RuntimeObjectID||reference.RuntimeComponentID) {
        if(reference.ComponentFileID||reference.RuntimeSceneID!=scene->GetInstanceID()||!reference.RuntimeObjectID||!reference.RuntimeComponentID)return nullptr;
        auto* object=_Runtime->QueryGameObject(reference.RuntimeObjectID);
        return object&&object->GetScene()==scene?LiveComponent(object,reference.RuntimeComponentID):nullptr;
    }
    if(!reference.ComponentFileID)return nullptr;
    return _Runtime->QuerySceneComponentByFileID(scene,reference.ComponentFileID);
}

bool ScriptEditor::GetSceneComponentChoices(const ScriptFieldTarget& target,const std::string& required,
    std::vector<ScriptComponentChoice>& choices,std::string& error) const {
    if(!ResolveFieldTarget(target,error))return false;
    auto* scene=_Scenes->GetActiveScene();std::vector<GameSproutRuntime::IGameObject*> nodes{scene};
    std::vector<ScriptComponentChoice> result;
    for(std::size_t index=0;index<nodes.size();++index){
        auto* object=nodes[index];
        for(std::size_t componentIndex=0;componentIndex<object->GetComponentCount();++componentIndex){
            auto* component=object->GetComponentAt(componentIndex);
            if(!component||!_Runtime->IsComponentType(component,required))continue;
            std::string name=component->GetTypeName();
            for(const auto& type:_Runtime->GetBuiltinComponentTypes())if(type.TypeID==name){name=type.Name;break;}
            if(auto* script=dynamic_cast<GameSproutRuntime::IScriptComponent*>(component))
                for(const auto& type:_Types)if(type.Type.TypeID==script->GetScriptTypeID()){name=type.Type.Name;break;}
            uint64_t fileID=0;_Runtime->GetSceneComponentFileID(component,fileID);
            result.push_back({{_FieldSessionID,scene->GetInstanceID(),object->GetInstanceID()},component->GetComponentInstanceID(),name,
                ObjectPath(*object)+" / "+name+" #"+std::to_string(component->GetComponentInstanceID()),required,fileID});
        }
        for(std::size_t child=0;child<object->GetChildCount();++child)if(auto* item=object->GetChild(child))nodes.push_back(item);
    }
    choices.swap(result);error.clear();return true;
}

bool ScriptEditor::ReferenceFromComponent(const ScriptFieldTarget& target,const ScriptObjectTarget& source,uint64_t instance,
    const std::string& required,GameSproutRuntime::ScriptValue& value,std::string& error) const {
    if(!ResolveFieldTarget(target,error))return false;
    if(source.SessionID!=_FieldSessionID||source.WorldID!=target.WorldID){error="组件所属会话已经失效或不属于当前场景。";return false;}
    auto* scene=_Scenes->GetActiveScene();auto* object=_Runtime->QueryGameObject(source.ObjectID);
    auto* component=object&&object->GetScene()==scene?LiveComponent(object,instance):nullptr;
    if(!component||!_Runtime->IsComponentType(component,required)){error="组件已移除或不符合字段要求的类型。";return false;}
    if(!_Editing||!_Editing->CanEdit({target.WorldID,target.ObjectID,0},error))return false;
    GameSproutRuntime::ScriptValue candidate;candidate.Kind=GameSproutRuntime::ScriptValueKind::Component;
    uint64_t fileID=0;
    if(_Scenes->IsPlaySessionActive()){
        if(_Runtime->GetSceneComponentFileID(component,fileID))candidate.Component={true,fileID};
        else candidate.Component={true,0,scene->GetInstanceID(),object->GetInstanceID(),instance};
    }else {
        // Authoring allocation is an explicit picker/drop operation. Browsing
        // candidates and Play never capture or allocate author document IDs.
        fileID=_Scenes->GetComponentFileID(object->GetInstanceID(),instance);
        if(!fileID){error="无法为所选组件取得可保存的场景身份。";return false;}
        candidate.Component={true,fileID};
    }
    value=std::move(candidate);error.clear();return true;
}

bool ScriptEditor::ComponentReferenceFromObject(const ScriptFieldTarget& target,const ScriptObjectTarget& source,
    const std::string& required,GameSproutRuntime::ScriptValue& value,std::string& error) const {
    if(!ResolveFieldTarget(target,error))return false;
    if(source.SessionID!=_FieldSessionID||source.WorldID!=target.WorldID){error="拖入对象所属会话已经失效。";return false;}
    auto* scene=_Scenes->GetActiveScene();auto* object=_Runtime->QueryGameObject(source.ObjectID);
    if(!object||object->GetScene()!=scene){error="只能引用当前场景的组件。";return false;}
    uint64_t match=0;
    for(std::size_t index=0;index<object->GetComponentCount();++index){
        auto* component=object->GetComponentAt(index);if(!component||!_Runtime->IsComponentType(component,required))continue;
        if(match){error="此对象有多个符合要求的组件，请点击选择按钮明确选择，或直接拖入组件标题。";return false;}
        match=component->GetComponentInstanceID();
    }
    if(!match){error="此对象没有字段所需类型的组件。";return false;}
    return ReferenceFromComponent(target,source,match,required,value,error);
}

bool ScriptEditor::LocateComponentReference(const ScriptFieldTarget& target,const GameSproutRuntime::ScriptValue& value,std::string& error) {
    if(!ResolveFieldTarget(target,error))return false;
    if(!_Selection||value.Kind!=GameSproutRuntime::ScriptValueKind::Component){error="组件引用或选择服务不可用。";return false;}
    auto* component=ResolveComponentReference(value.Component);
    if(!component){error="引用组件已经移除。";return false;}
    _Selection->SetSceneObjectSelection({target.WorldID,component->GetGameObject()->GetInstanceID()});error.clear();return true;
}
