#include "ScriptComponent.h"
#include "../GameObject.h"
#include "../Scene.h"
#include <exception>
#include <cstring>

namespace GameSproutRuntime {
namespace {
std::string ReferenceKey(const ScriptValue& key) {
    switch(key.Kind){
    case ScriptValueKind::Boolean:return key.Boolean?"b1":"b0";
    case ScriptValueKind::Integer:return "i"+std::to_string(key.Integer);
    case ScriptValueKind::Unsigned:return "u"+std::to_string(key.Unsigned);
    case ScriptValueKind::String:return "s"+std::to_string(key.String.size())+":"+key.String;
    case ScriptValueKind::Number:{std::uint64_t bits=0;static_assert(sizeof(bits)==sizeof(key.Number));std::memcpy(&bits,&key.Number,sizeof(bits));return "n"+std::to_string(bits);}
    default:return "invalid";
    }
}
template<typename Visitor> void VisitReferences(ScriptValue& value,const Visitor& visitor,const std::string& path={}) {
    if(value.Kind==ScriptValueKind::SceneObject||value.Kind==ScriptValueKind::Component)visitor(path,value);
    for(auto& [id,child]:value.Fields)VisitReferences(child,visitor,path+"/s"+std::to_string(id.size())+":"+id);
    for(std::size_t index=0;index<value.Elements.size();++index)VisitReferences(value.Elements[index],visitor,path+"/i"+std::to_string(index));
    for(auto& item:value.Entries){const auto entryPath=path+"/d"+ReferenceKey(item.first);VisitReferences(item.first,visitor,entryPath+"/k");VisitReferences(item.second,visitor,entryPath+"/v");}
}

template<typename Visitor> void VisitTypedReferences(ScriptValue& value,const ScriptFieldInfo& schema,
    const Visitor& visitor,const std::string& path={}) {
    if(value.Kind==ScriptValueKind::Component)visitor(path,schema);
    if(value.Kind==ScriptValueKind::Structure)for(const auto& field:schema.Children) {
        auto found=value.Fields.find(field.ID);if(found!=value.Fields.end())VisitTypedReferences(found->second,field,visitor,path+"/s"+std::to_string(field.ID.size())+":"+field.ID);
    }
    if(value.Kind==ScriptValueKind::Sequence&&schema.Children.size()==1)
        for(std::size_t index=0;index<value.Elements.size();++index)VisitTypedReferences(value.Elements[index],schema.Children[0],visitor,path+"/i"+std::to_string(index));
    if(value.Kind==ScriptValueKind::Dictionary&&schema.Children.size()==2)
        for(auto& item:value.Entries){const auto entryPath=path+"/d"+ReferenceKey(item.first);VisitTypedReferences(item.first,schema.Children[0],visitor,entryPath+"/k");VisitTypedReferences(item.second,schema.Children[1],visitor,entryPath+"/v");}
}

}
ScriptReferenceContext ScriptComponent::ReferenceContext() const {
    ScriptReferenceContext context;
    context.EncodeObject=[this](const IGameObject* target,ScriptObjectReference& result,std::string& error){
        result={};if(!target)return true;
        auto* node=_Runtime.Find(const_cast<IGameObject*>(target));
        if(!node||!node->SceneOwner||node->SceneOwner!=_GameObject->GetScene()){error="Native object reference points outside its owner's live scene";return false;}
        std::uint64_t fileID=0;
        if(_Runtime.GetSceneObjectFileID(target,fileID))result={true,fileID};
        else result={true,0,node->SceneOwner->GetInstanceID(),node->ID};
        return true;
    };
    context.DecodeObject=[this](const ScriptObjectReference& reference,IGameObject*& target,std::string& error){
        target=nullptr;if(!reference.Assigned)return true;
        target=reference.RuntimeObjectID?_Runtime.QueryGameObject(reference.RuntimeObjectID):_Runtime.QuerySceneObjectByFileID(_GameObject->GetScene(),reference.ObjectFileID);
        if(reference.RuntimeObjectID&&(!_Runtime.IsScriptExecutionEnabled()||!target||target->GetScene()!=_GameObject->GetScene()||target->GetScene()->GetInstanceID()!=reference.RuntimeSceneID)){error="Runtime object reference is stale or belongs to another scene";return false;}
        return true;
    };
    context.EncodeComponent=[this](const IComponent* target,ScriptComponentReference& result,std::string& error){
        result={};if(!target)return true;
        auto* component=_Runtime.NormalizeComponent(target);
        if(!component){error="Native component reference is stale";return false;}
        auto* node=_Runtime.Find(component->GetGameObject());
        if(!node||!node->SceneOwner||node->SceneOwner!=_GameObject->GetScene()){error="Native component reference points outside its owner's live scene";return false;}
        std::uint64_t fileID=0;
        if(_Runtime.GetSceneComponentFileID(component,fileID))result={true,fileID};
        else result={true,0,node->SceneOwner->GetInstanceID(),node->ID,component->GetComponentInstanceID()};
        return true;
    };
    context.DecodeComponent=[this](const ScriptComponentReference& reference,bool native,IComponent*& target,std::string& error){
        target=nullptr;if(!reference.Assigned)return true;
        if(reference.RuntimeComponentID){
            auto* node=_Runtime.Find(reference.RuntimeObjectID);
            if(!_Runtime.IsScriptExecutionEnabled()||!_Runtime.IsInScene(node)||node->SceneOwner!=_GameObject->GetScene()||node->SceneOwner->GetInstanceID()!=reference.RuntimeSceneID){error="Runtime component reference is stale or belongs to another scene";return false;}
            for(const auto& entry:node->Components)if(!entry.PendingRemove&&entry.Serial==reference.RuntimeComponentID){target=entry.Pointer;break;}
            if(!target){error="Runtime component reference is stale";return false;}
        }else target=_Runtime.QuerySceneComponentByFileID(_GameObject->GetScene(),reference.ComponentFileID);
        if(native&&target)if(auto* script=dynamic_cast<ScriptComponent*>(target))target=script->GetNativeInstance();
        return true;
    };
    return context;
}
bool ScriptComponent::ValidateReferences(const ScriptFieldInfo& schema,const ScriptValue& value,std::string& error) const {
    const auto context=ReferenceContext();
    if(value.Kind==ScriptValueKind::SceneObject){IGameObject* target=nullptr;return context.DecodeObject(value.SceneObject,target,error);}
    if(value.Kind==ScriptValueKind::Component){IComponent* target=nullptr;if(!context.DecodeComponent(value.Component,false,target,error))return false;if(target&&!schema.ComponentType.empty()&&!_Runtime.IsComponentType(target,schema.ComponentType)){error="Referenced component does not match "+schema.ComponentType;return false;}return true;}
    if(value.Kind==ScriptValueKind::Structure){for(const auto& child:schema.Children){const auto found=value.Fields.find(child.ID);if(found!=value.Fields.end()&&!ValidateReferences(child,found->second,error))return false;}}
    else if(value.Kind==ScriptValueKind::Sequence&&schema.Children.size()==1){for(const auto& child:value.Elements)if(!ValidateReferences(schema.Children[0],child,error))return false;}
    else if(value.Kind==ScriptValueKind::Dictionary&&schema.Children.size()==2){for(const auto& item:value.Entries)if(!ValidateReferences(schema.Children[0],item.first,error)||!ValidateReferences(schema.Children[1],item.second,error))return false;}
    return true;
}
bool ScriptComponent::ApplyNativeField(const ScriptFieldRegistration& field,const ScriptValue& value,std::string& error) {
    const auto context=ReferenceContext();
    if(!field.Write||!field.Write(*_Instance,value,context,error))return false;
    auto copy=value;auto& missing=_MissingReferences[field.Info.ID];missing.clear();
    VisitReferences(copy,[&](const std::string& path,ScriptValue& child){
        if(child.Kind==ScriptValueKind::SceneObject&&child.SceneObject.Assigned&&!child.SceneObject.RuntimeObjectID){IGameObject* target=nullptr;if(context.DecodeObject(child.SceneObject,target,error)&&!target)missing.emplace(path,child);}
        if(child.Kind==ScriptValueKind::Component&&child.Component.Assigned&&!child.Component.RuntimeComponentID){IComponent* target=nullptr;if(context.DecodeComponent(child.Component,true,target,error)&&!target)missing.emplace(path,child);}
    });
    if(missing.empty())_MissingReferences.erase(field.Info.ID);
    return true;
}
bool ScriptComponent::InvalidateReferences(IGameObject* object,IComponent* component,bool nativeOnly) {
    const auto context=ReferenceContext();ScriptObjectReference objectReference;ScriptComponentReference componentReference;std::string error;
    if(object&&!context.EncodeObject(object,objectReference,error))return false;
    if(component&&!context.EncodeComponent(component,componentReference,error))return false;
    const auto matches=[&](const ScriptValue& value){return (object&&value.Kind==ScriptValueKind::SceneObject&&value.SceneObject==objectReference)||(component&&value.Kind==ScriptValueKind::Component&&value.Component==componentReference);};
    bool changed=false;
    if(!nativeOnly)for(auto& [id,value]:_Fields)VisitReferences(value,[&](const std::string&,ScriptValue& child){if(matches(child)){if((child.Kind==ScriptValueKind::SceneObject&&child.SceneObject.RuntimeObjectID)||(child.Kind==ScriptValueKind::Component&&child.Component.RuntimeComponentID)){child.SceneObject={};child.Component={};}changed=true;}});
    if(!_Instance)return changed;
    const auto* type=_Runtime.FindScriptType(_TypeID);if(!type)return changed;
    try {for(const auto& field:type->Fields){
        ScriptValue live;if(!field.Read(*_Instance,live,context,error)){Fail("Cannot inspect references: "+error);return true;}
        std::set<std::string> nativePaths;
        if(nativeOnly)VisitTypedReferences(live,field.Info,[&](const std::string& path,const ScriptFieldInfo& schema){
            if(schema.ComponentType=="GameSproutRuntime.Script"||_Runtime.FindScriptType(schema.ComponentType)!=nullptr)nativePaths.insert(path);
        });
        bool affected=false;
        std::map<std::string,ScriptValue> authoredLeaves;
        if(const auto authored=_Fields.find(field.Info.ID);authored!=_Fields.end()){
            auto authoredValue=authored->second;
            VisitReferences(authoredValue,[&](const std::string& path,ScriptValue& child){authoredLeaves.emplace(path,child);});
        }
        VisitReferences(live,[&](const std::string& path,ScriptValue& child){if(matches(child)&&(!nativeOnly||nativePaths.contains(path))){
            if(const auto authored=authoredLeaves.find(path);authored!=authoredLeaves.end()&&authored->second==child&&
               (nativeOnly||(child.Kind==ScriptValueKind::SceneObject&&!child.SceneObject.RuntimeObjectID)||(child.Kind==ScriptValueKind::Component&&!child.Component.RuntimeComponentID)))_MissingReferences[field.Info.ID][path]=child;
            child.SceneObject={};child.Component={};affected=true;
        }});
        if(affected){if(!field.Write(*_Instance,live,context,error)){Fail("Cannot invalidate reference: "+error);return true;}changed=true;}
    }}
    catch(const std::exception& exception){Fail(exception.what());}catch(...){Fail("Cannot invalidate native reference");}
    return changed;
}
bool ScriptComponent::InvalidateObjectReference(IGameObject* object){return object&&InvalidateReferences(object,nullptr);}
bool ScriptComponent::InvalidateComponentReference(IComponent* component){return component&&InvalidateReferences(nullptr,component);}
bool ScriptComponent::InvalidateNativeInstanceReference(ScriptComponent* component){return component&&InvalidateReferences(nullptr,component,true);}
bool ScriptComponent::RebindMissingObjectReferences() {
    if(!_Instance||_MissingReferences.empty())return false;
    const auto* type=_Runtime.FindScriptType(_TypeID);if(!type)return false;
    const auto context=ReferenceContext();bool changed=false;
    try {for(const auto& field:type->Fields){
        auto missing=_MissingReferences.find(field.Info.ID);if(missing==_MissingReferences.end())continue;
        ScriptValue live;std::string error;if(!field.Read(*_Instance,live,context,error)){Fail(error);return true;}
        bool restored=false;
        VisitReferences(live,[&](const std::string& path,ScriptValue& child){
            const auto leaf=missing->second.find(path);if(leaf==missing->second.end()||leaf->second.Kind!=child.Kind)return;
            if((child.Kind==ScriptValueKind::SceneObject&&child.SceneObject.Assigned)||(child.Kind==ScriptValueKind::Component&&child.Component.Assigned)){missing->second.erase(leaf);return;}
            bool ready=false;
            if(child.Kind==ScriptValueKind::SceneObject){IGameObject* target=nullptr;ready=context.DecodeObject(leaf->second.SceneObject,target,error)&&target;}
            else {IComponent* target=nullptr;ready=context.DecodeComponent(leaf->second.Component,true,target,error)&&target;}
            if(ready){child=leaf->second;missing->second.erase(leaf);restored=true;}
        });
        if(restored){if(!field.Write(*_Instance,live,context,error)){Fail(error);return true;}changed=true;}
        if(missing->second.empty())_MissingReferences.erase(missing);
    }}
    catch(const std::exception& exception){Fail(exception.what());}catch(...){Fail("Cannot restore native reference");}
    return changed;
}
}
