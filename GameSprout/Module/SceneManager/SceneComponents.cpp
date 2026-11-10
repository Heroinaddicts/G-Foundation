#include "SceneManager.h"
#include "SceneAnimationDocument.h"
#include "ScenePhysicsDocument.h"
#include <algorithm>
#include <exception>
#include <stdexcept>
#include <set>

namespace {
class ComponentEditingScope {
public:
    explicit ComponentEditingScope(bool& flag):_Flag(flag){_Flag=true;}
    ~ComponentEditingScope(){_Flag=false;}
private:
    bool& _Flag;
};
GameSproutRuntime::IComponent* Find(GameSproutRuntime::IGameObject& object,const std::string& type,
                                   uint64_t instanceID,std::size_t* index=nullptr,uint64_t componentInstanceID=0) {
    for(std::size_t i=0;i<object.GetComponentCount();++i) {
        auto* component=object.GetComponentAt(i);
        if(!component||!component->GetTypeName()||type!=component->GetTypeName())continue;
        if(componentInstanceID&&component->GetComponentInstanceID()!=componentInstanceID)continue;
        if(type=="GameSproutRuntime.ScriptComponent") {
            auto* script=dynamic_cast<GameSproutRuntime::IScriptComponent*>(component);
            if(!script||script->GetScriptInstanceID()!=instanceID)continue;
        }
        if(index)*index=i;
        return component;
    }
    return nullptr;
}
GameSproutRuntime::IComponent* Find(GameSproutRuntime::IGameObject& object,const Json::Value& data,const SceneDocument::ObjectIDs& ids) {
    if(data["ID"].isUInt64()) {
        for(std::size_t i=0;i<object.GetComponentCount();++i) {
            auto* component=object.GetComponentAt(i);const auto found=ids.FindComponentFileID(component);
            if(found==data["ID"].asUInt64()&&data["Type"]==component->GetTypeName())return component;
        }
        return nullptr;
    }
    return Find(object,data["Type"].asString(),data["InstanceID"].isUInt64()?data["InstanceID"].asUInt64():0);
}
bool Remove(GameSproutRuntime::IGameSproutRuntime& runtime,GameSproutRuntime::IGameObject& object,
            const Json::Value& data,const SceneDocument::ObjectIDs& ids,std::string& error) {
    if(auto* component=Find(object,data,ids)) {
        if(object.RemoveComponentInstance(component->GetComponentInstanceID()))return true;
        error="Runtime 拒绝移除组件实例。";return false;
    }
    if(data["ID"].isUInt64()){error="组件文档身份已经不存在。";return false;}
    if(data["Type"]=="GameSproutRuntime.ScriptComponent")
        return runtime.RemoveScriptComponent(&object,data["InstanceID"].asUInt64(),error);
    return runtime.RemoveBuiltinComponent(&object,data["Type"].asString(),error);
}
void BindComponent(const Json::Value& data,uint64_t fileID,GameSproutRuntime::IGameSproutRuntime& runtime,
                   GameSproutRuntime::IScene& scene) {
    if(!SceneAnimationDocument::Handles(data["Type"].asString()))return;
    Json::Value snapshot(Json::arrayValue),item(Json::objectValue);
    item["Owner"]=Json::UInt64(fileID);item["Data"]=data;snapshot.append(std::move(item));
    SceneAnimationDocument::RestoreBindings(snapshot,runtime,scene);
}
}

bool SceneManager::AddBuiltinComponent(uint64_t objectID,const std::string& typeName,std::string& error) {
    return ChangeComponent(objectID,typeName,0,{}, {},true,error);
}
bool SceneManager::RemoveComponent(uint64_t objectID,const std::string& typeName,std::string& error) {
    if(typeName=="GameSproutRuntime.ScriptComponent") {error="请按挂载实例 ID 移除脚本组件。";return false;}
    return ChangeComponent(objectID,typeName,0,{}, {},false,error);
}
bool SceneManager::AttachScriptComponent(uint64_t objectID,const std::string& typeID,
    const GameSproutRuntime::ResourceReference& source,std::string& error) {
    return ChangeComponent(objectID,"GameSproutRuntime.ScriptComponent",0,typeID,source,true,error);
}
bool SceneManager::RemoveScriptComponent(uint64_t objectID,uint64_t instanceID,std::string& error) {
    if(!instanceID){error="脚本组件实例 ID 必须非零。";return false;}
    return ChangeComponent(objectID,"GameSproutRuntime.ScriptComponent",instanceID,{}, {},false,error);
}

bool SceneManager::ChangeComponent(uint64_t objectID,const std::string& typeName,uint64_t scriptInstanceID,
    const std::string& scriptTypeID,const GameSproutRuntime::ResourceReference& source,bool adding,std::string& error,uint64_t componentInstanceID) {
    if(!Ready(error))return false;
    auto* scene=GetActiveScene();auto* object=Runtime()->QueryGameObject(objectID);
    if(!object||object->GetScene()!=scene||object==scene) {error="组件操作需要当前场景中的普通节点。";return false;}
    if(typeName=="GameSproutRuntime.Transform") {error="Transform 是节点的固有组件，不能添加或移除。";return false;}
    if(adding&&typeName=="GameSproutRuntime.ScriptComponent") {
        for(std::size_t i=0;i<object->GetComponentCount();++i)
            if(const auto* script=dynamic_cast<GameSproutRuntime::IScriptComponent*>(object->GetComponentAt(i));
                script&&script->GetScriptTypeID()==scriptTypeID) {
                error="同一对象只能挂载一个相同类型的脚本组件。";return false;
            }
    }
    Edit edit;edit.Type=Edit::Kind::Component;
    edit.Label=adding?"添加组件":"移除组件";
    auto* existing=adding&&ScenePhysicsDocument::IsCollider(typeName)?nullptr:
        Find(*object,typeName,scriptInstanceID,&edit.ComponentIndex,componentInstanceID);
    if(adding&&existing) {error="节点已经挂载该组件。";return false;}
    if(!adding&&!existing) {error="要移除的组件已经不存在。";return false;}
    if(!SynchronizeDirtyState(error))return false;
    try {
        edit.FileID=_ObjectIDs.RuntimeToFile.at(objectID);
        _History.reserve(_HistoryPosition+1);
        if(existing)edit.ComponentBefore=SceneDocument::SerializeComponent(*existing,_ObjectIDs);
    } catch(const std::exception& exception) {error=exception.what();return false;}
    catch(...) {error="无法创建组件撤销快照。";return false;}
    edit.ComponentWasMainCamera=existing&&existing==scene->GetMainCamera();
    ComponentEditingScope editing(_ExecutingEdit);
    bool success=false;
    if(!adding)success=RunComponentEdit(edit,true,error);
    else {
        GameSproutRuntime::IComponent* created=nullptr;
        edit.ComponentIndex=object->GetComponentCount();
        try {
            if(typeName=="GameSproutRuntime.ScriptComponent")
                created=Runtime()->AddScriptComponent(object,scriptTypeID,source,0,error);
            else created=Runtime()->AddBuiltinComponent(object,typeName,error);
            if(created) {
                success=SynchronizeDirtyState(error);
                if(success)edit.ComponentAfter=SceneDocument::SerializeComponent(*created,_ObjectIDs);
            }
        } catch(const std::exception& exception) {error=exception.what();}
        catch(...) {error="添加组件时发生异常。";}
        if(!success&&created) {
            std::string rollbackError;
            // 即使序列化失败，也能按公共接口标识移除本次创建的组件。
            bool rolledBack=false;
            rolledBack=object->RemoveComponentInstance(created->GetComponentInstanceID());
            if(!rolledBack)error+=" 恢复添加前状态失败："+rollbackError;
        }
    }
    if(success) {
        _History.resize(_HistoryPosition);_History.push_back(std::move(edit));++_HistoryPosition;
    } else if(error.empty())error="Runtime 拒绝组件操作。";
    RefreshState(true);
    return success;
}

bool SceneManager::RunComponentEdit(const Edit& edit,bool forward,std::string& error) {
    auto* object=QueryObjectByFileID(edit.FileID);auto* scene=GetActiveScene();
    if(!object||object->GetScene()!=scene){error="组件所属节点已经删除。";return false;}
    const auto& before=forward?edit.ComponentBefore:edit.ComponentAfter;
    const auto& after=forward?edit.ComponentAfter:edit.ComponentBefore;
    const auto& identity=before.isNull()?after:before;
    auto* existing=Find(*object,identity,_ObjectIDs);
    if(!SynchronizeDirtyState(error))return false;
    try {
        if(before.isNull()?existing!=nullptr:(!existing||SceneDocument::SerializeComponent(*existing,_ObjectIDs)!=before)) {
            error="组件已在事务之外变化，不能执行该撤销/重做。";return false;
        }
        if(before.isNull()&&!after.isNull()&&!ScenePhysicsDocument::IsCollider(after["Type"].asString())) {
            const auto type=after["Type"].asString();
            for(std::size_t i=0;i<object->GetComponentCount();++i) {
                const auto* component=object->GetComponentAt(i);
                if(type!=component->GetTypeName())continue;
                const auto* script=dynamic_cast<const GameSproutRuntime::IScriptComponent*>(component);
                if(!script||script->GetScriptTypeID()==after["ScriptTypeID"].asString()) {
                    error="节点已挂载同类组件，无法恢复旧组件。";return false;
                }
            }
        }
        if(after.isNull()&&before["Type"]=="GameSproutRuntime.Animator") {
            for(const auto file:_DirtyState.AnimationOwners)if(auto* owner=QueryObjectByFileID(file))
                if(auto* skin=owner->GetComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>();
                    skin&&skin->GetAnimatorObjectID()==object->GetInstanceID()) {
                    error="仍有 SkinnedMeshRenderer 引用该 Animator。";return false;
                }
        }
    } catch(const std::exception& exception) {error=exception.what();return false;}
    auto* runtime=Runtime();
    const auto objectID=object->GetInstanceID();
    bool changed=false,constructionOpen=false;
    // RestoreComponent has its own nestable scope. Keep the outer transaction
    // open until ordering, animation references and MainCamera are restored too.
    if(!runtime->BeginConstruction(error))return false;
    constructionOpen=true;
    const auto restore=[&](const Json::Value& data,std::string& reason) {
        std::set<uint64_t> previous;
        for(std::size_t i=0;i<object->GetComponentCount();++i)previous.insert(object->GetComponentAt(i)->GetComponentInstanceID());
        if(!SceneDocument::RestoreComponent(data,*object,*Runtime(),reason))return false;
        auto* component=Find(*object,data,_ObjectIDs);
        if(!component)for(std::size_t i=0;i<object->GetComponentCount();++i) {
            auto* candidate=object->GetComponentAt(i);
            if(!previous.count(candidate->GetComponentInstanceID())&&data["Type"]==candidate->GetTypeName()){component=candidate;break;}
        }
        if(!component||!object->SetComponentIndex(component,edit.ComponentIndex)) {reason="Runtime 拒绝恢复组件顺序。";return false;}
        if(data["ID"].isUInt64()) {
            const auto file=data["ID"].asUInt64();
            if(!scene->BindAuthoringIdentities({},{{objectID,component->GetComponentInstanceID(),file}},reason))return false;
            _ObjectIDs.BindComponentFileID(component,file);
        }
        BindComponent(data,edit.FileID,*Runtime(),*scene);
        if(edit.ComponentWasMainCamera&&!scene->SetMainCamera(object->GetComponent<GameSproutRuntime::ICameraComponent>())) {
            reason="Runtime 拒绝恢复主相机。";return false;
        }
        return true;
    };
    try {
        if(after.isNull()) {
            const auto instance=existing->GetComponentInstanceID();
            changed=Remove(*runtime,*object,before,_ObjectIDs,error);
            if(!changed)throw std::runtime_error(error.empty()?"Runtime 拒绝移除组件。":error);
            _ObjectIDs.ForgetComponentFileID(objectID,instance);
        }
        else {
            changed=true;
            if(!restore(after,error))throw std::runtime_error(error);
        }
        if(SynchronizeDirtyState(error)) {
            constructionOpen=false;
            if(!runtime->EndConstruction(error))throw std::runtime_error(error.empty()?"Runtime 无法完成组件初始化。":error);
            if(runtime->QueryGameObject(objectID)!=object)throw std::runtime_error("组件所属节点在初始化时已被销毁。");
            return true;
        }
    } catch(const std::exception& exception) {error=exception.what();}
    catch(...) {error="组件事务执行失败。";}
    if(changed) {
        std::string rollbackError;
        try {
            // A failed outer commit already closes its scope. Hold a fresh
            // barrier while restoring the previous component and its bindings.
            if(!constructionOpen)constructionOpen=runtime->BeginConstruction(rollbackError);
            object=runtime->QueryGameObject(objectID);scene=GetActiveScene();
            bool restored=constructionOpen&&object&&scene&&object->GetScene()==scene;
            if(restored&&!after.isNull()&&Find(*object,after,_ObjectIDs))restored=Remove(*runtime,*object,after,_ObjectIDs,rollbackError);
            if(restored&&!before.isNull())restored=restore(before,rollbackError);
            if(!restored)error+=" 恢复组件原值失败："+rollbackError;
        } catch(...) {error+=" 恢复组件原值时发生异常。";}
    }
    if(constructionOpen) {
        constructionOpen=false;
        try {
            std::string cleanup;
            if(!runtime->EndConstruction(cleanup))error+=" 结束组件恢复事务失败："+cleanup;
        } catch(...) {error+=" 结束组件恢复事务时发生异常。";}
    }
    return false;
}
