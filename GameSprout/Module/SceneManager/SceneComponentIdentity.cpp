#include "SceneManager.h"

uint64_t SceneManager::GetComponentFileID(uint64_t objectID,uint64_t componentInstanceID) const {
    auto* runtime=Runtime();auto* object=runtime?runtime->QueryGameObject(objectID):nullptr;
    if(!object||object->GetScene()!=GetActiveScene()||!componentInstanceID)return 0;
    for(std::size_t i=0;i<object->GetComponentCount();++i) {
        auto* component=object->GetComponentAt(i);
        if(!component||component->GetComponentInstanceID()!=componentInstanceID)continue;
        auto found=_ObjectIDs.FindComponentFileID(component);
        if(!found&&!runtime->IsExecuting()&&!IsPlaySessionActive()&&!_Switching&&!_ExecutingEdit) {
            // Explicit editor operations may reach a newly created Runtime
            // component before the next scene event flush. Publish its stable
            // document identity now so its Undo closure survives recreation.
            Json::Value document;std::string text,error;
            if(!Capture(document,text,error))return 0;
            found=_ObjectIDs.FindComponentFileID(component);
        }
        return found;
    }
    return 0;
}
GameSproutRuntime::IComponent* SceneManager::QueryComponentByFileID(uint64_t objectID,uint64_t componentFileID) const {
    auto* runtime=Runtime();auto* object=runtime?runtime->QueryGameObject(objectID):nullptr;
    if(!object||object->GetScene()!=GetActiveScene()||!componentFileID)return nullptr;
    for(std::size_t i=0;i<object->GetComponentCount();++i) {
        auto* component=object->GetComponentAt(i);
        const auto found=_ObjectIDs.FindComponentFileID(component);
        if(found==componentFileID)return component;
    }
    return nullptr;
}
bool SceneManager::RemoveComponentInstance(uint64_t objectID,uint64_t componentInstanceID,std::string& error) {
    if(!Ready(error))return false;
    auto* object=Runtime()->QueryGameObject(objectID);
    if(object&&object->GetScene()==GetActiveScene())for(std::size_t i=0;i<object->GetComponentCount();++i) {
        auto* component=object->GetComponentAt(i);
        if(!component||component->GetComponentInstanceID()!=componentInstanceID)continue;
        const auto* script=dynamic_cast<GameSproutRuntime::IScriptComponent*>(component);
        return ChangeComponent(objectID,component->GetTypeName(),script?script->GetScriptInstanceID():0,
            {},{},false,error,componentInstanceID);
    }
    error="要移除的组件实例已经不存在。";return false;
}
