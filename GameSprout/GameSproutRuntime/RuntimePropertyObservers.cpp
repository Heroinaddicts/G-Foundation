#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Scene.h"
#include "Components/ScriptComponent.h"
#include <limits>

namespace GameSproutRuntime {
bool Runtime::RegisterObjectPropertyObserver(std::uint64_t objectID,IRuntimePropertyObserver* observer) {
    auto* object=Find(objectID);
    if(!observer||!IsInScene(object)||_NextPropertyObserver==std::numeric_limits<std::uint64_t>::max())return false;
    auto& observation=_PropertyObservations[objectID];
    if(observation.Observers.contains(observer))return false;
    const bool first=observation.Observers.empty();
    observation.SceneID=object->SceneOwner->GetInstanceID();
    observation.Observers.emplace(observer,++_NextPropertyObserver);
    if(first) {
        CheckObservedScriptProperties(objectID);
        const auto current=_PropertyObservations.find(objectID);
        if(current==_PropertyObservations.end()||!current->second.Observers.contains(observer))return false;
        current->second.Pending=false;
    }
    return true;
}
void Runtime::UnregisterObjectPropertyObserver(std::uint64_t objectID,IRuntimePropertyObserver* observer) {
    const auto found=_PropertyObservations.find(objectID);
    if(found==_PropertyObservations.end())return;
    found->second.Observers.erase(observer);
    if(found->second.Observers.empty())_PropertyObservations.erase(found);
}
void Runtime::MarkObjectPropertiesChanged(std::uint64_t objectID) {
    const auto found=_PropertyObservations.find(objectID);
    if(found!=_PropertyObservations.end())found->second.Pending=true;
}
void Runtime::CheckObservedScriptProperties(std::uint64_t objectID) {
    if(!_PropertyObservations.contains(objectID)||_ReadingObservedProperties)return;
    auto* node=Find(objectID);
    if(!IsInScene(node))return;
    _ReadingObservedProperties=true;
    struct Guard{bool& Flag;~Guard(){Flag=false;}} guard{_ReadingObservedProperties};
    RuntimeOperation operation(*this);
    std::map<std::uint64_t,std::map<std::string,ScriptValue>> values;
    const auto components=node->Components;
    for(const auto& component:components) {
        if(component.PendingRemove)continue;
        auto* script=dynamic_cast<ScriptComponent*>(component.Pointer);
        if(!script)continue;
        std::string error;std::map<std::string,ScriptValue> fields;
        const auto instanceID=script->GetComponentInstanceID();
        if(script->ReadFields(fields,error))values.emplace(instanceID,std::move(fields));
    }
    const auto observation=_PropertyObservations.find(objectID);
    if(observation==_PropertyObservations.end())return;
    if(values!=observation->second.ScriptFields) {
        observation->second.ScriptFields.swap(values);observation->second.Pending=true;
    }
}
void Runtime::RefreshObservedScriptProperties() {
    if(!_ObservedScriptFieldsDirty)return;
    _ObservedScriptFieldsDirty=false;
    std::vector<std::uint64_t> ids;
    for(const auto& [id,observation]:_PropertyObservations)ids.push_back(id);
    for(const auto id:ids)CheckObservedScriptProperties(id);
}
void Runtime::DispatchObjectPropertyChanges() {
    std::vector<std::uint64_t> pending;
    for(auto& [id,observation]:_PropertyObservations)if(observation.Pending){observation.Pending=false;pending.push_back(id);}
    for(const auto id:pending) {
        auto found=_PropertyObservations.find(id);auto* node=Find(id);
        if(found==_PropertyObservations.end())continue;
        if(!IsInScene(node)){_PropertyObservations.erase(found);continue;}
        const auto observers=found->second.Observers;const auto sceneID=node->SceneOwner->GetInstanceID();
        RuntimeOperation operation(*this);
        for(const auto& [observer,sequence]:observers) {
            found=_PropertyObservations.find(id);
            if(found==_PropertyObservations.end())break;
            const auto current=found->second.Observers.find(observer);
            if(current==found->second.Observers.end()||current->second!=sequence)continue;
            try{observer->OnRuntimeObjectPropertiesChanged(sceneID,id);}catch(...){}
        }
    }
}
}
