#include "SceneManager.h"
#include "SceneAnimationDocument.h"
#include <exception>
#include <set>

namespace {
class EditingScope {
public:
    explicit EditingScope(bool& flag):_Flag(flag) {_Flag=true;}
    ~EditingScope() {_Flag=false;}
private:
    bool& _Flag;
};
}

uint64_t SceneManager::GetObjectFileID(uint64_t objectID) const {
    auto* scene=GetActiveScene();auto* object=Runtime()?Runtime()->QueryGameObject(objectID):nullptr;
    if(!scene||!object||object->GetScene()!=scene||object==scene)return 0;
    auto found=_ObjectIDs.RuntimeToFile.find(objectID);
    if(found!=_ObjectIDs.RuntimeToFile.end())return found->second;
    // 运行时新建节点没有作者文件身份，不为查询临时对象重新序列化整个世界。
    if(IsPlaySessionActive()||Runtime()->IsExecuting()) return 0;
    std::string error;
    if(!SynchronizeDirtyState(error))return 0;
    found=_ObjectIDs.RuntimeToFile.find(objectID);
    return found==_ObjectIDs.RuntimeToFile.end()?0:found->second;
}
GameSproutRuntime::IGameObject* SceneManager::QueryObjectByFileID(uint64_t fileID) const {
    auto* scene=GetActiveScene();if(!scene)return nullptr;
    if(!fileID)return scene;
    auto* object=Runtime()->QuerySceneObjectByFileID(scene,fileID);
    return object&&object->GetScene()==scene?object:nullptr;
}
bool SceneManager::CaptureDeletion(GameSproutRuntime::IGameObject& object,Edit& edit,std::string& error) const {
    auto* scene=GetActiveScene();auto* parent=object.Parent();
    if(IsTemporaryDocumentContext() && &object==GetDocumentContentRoot()){error="不能删除临时文档的内容根。";return false;}
    if(!scene||&object==scene||object.GetScene()!=scene||!parent){error="不能删除场景根或无效节点。";return false;}
    Json::Value subtree;
    // CaptureSubtree synchronizes the validated authoring snapshot before
    // reading the same live rows. The history entry only clears the root's
    // ParentID, and RestoreSubtree validates it again before Undo mutates Runtime.
    if(!CaptureSubtree(object,subtree,error,false))return false;
    edit.Type=Edit::Kind::Deletion;edit.FileID=_ObjectIDs.RuntimeToFile.at(object.GetInstanceID());
    edit.ParentFileID=parent==scene?0:_ObjectIDs.RuntimeToFile.at(parent->GetInstanceID());
    edit.SiblingIndex=parent->GetChildCount();
    for(std::size_t index=0;index<parent->GetChildCount();++index)if(parent->GetChild(index)==&object){edit.SiblingIndex=index;break;}
    if(edit.SiblingIndex==parent->GetChildCount()){error="节点不在父级列表中。";return false;}
    // Redo compares the subtree itself. New identities elsewhere must not make
    // an unchanged subtree different.
    subtree.removeMember("NextObjectID");
    std::set<uint64_t> files;
    for(const auto& node:subtree["Objects"])files.insert(node["ID"].asUInt64());
    const auto inside=[&](std::uint64_t id) {
        const auto found=_ObjectIDs.RuntimeToFile.find(id);
        return found!=_ObjectIDs.RuntimeToFile.end()&&files.contains(found->second);
    };
    edit.AnimationBindings=Json::Value(Json::arrayValue);
    for(const auto file:_DirtyState.AnimationOwners) {
        if(files.contains(file))continue;
        auto* owner=QueryObjectByFileID(file);if(!owner)continue;
        const auto remember=[&](GameSproutRuntime::IComponent& component) {
            Json::Value item(Json::objectValue);item["Owner"]=Json::UInt64(file);
            item["Data"]=SceneDocument::SerializeComponent(component,_ObjectIDs);edit.AnimationBindings.append(std::move(item));
        };
        if(auto* animator=owner->GetComponent<GameSproutRuntime::IAnimatorComponent>())
            for(const auto& binding:animator->GetNodeBindings())if(inside(binding.ObjectID)){remember(*animator);break;}
        if(auto* skin=owner->GetComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>())
            if(inside(skin->GetAnimatorObjectID()))remember(*skin);
    }
    edit.Subtree.swap(subtree);return true;
}
bool SceneManager::RunDeletion(const Edit& edit,bool forward,std::string& error,bool firstExecution) {
    auto* scene=GetActiveScene();if(!scene){error="当前场景已失效。";return false;}
    if(!forward) {
        if(!SynchronizeDirtyState(error))return false;
        if(QueryObjectByFileID(edit.FileID)){error="待恢复节点的稳定身份已经存在。";return false;}
        auto* parent=QueryObjectByFileID(edit.ParentFileID);
        if(!parent){error="待恢复子树的父节点已经删除。";return false;}
        GameSproutRuntime::IGameObject* restored=nullptr;
        if(!SceneDocument::RestoreSubtree(edit.Subtree,*Runtime(),*scene,*parent,edit.SiblingIndex,_ObjectIDs,restored,error))return false;
        SceneAnimationDocument::RestoreBindings(edit.AnimationBindings,*Runtime(),*scene);
        return true;
    }
    auto* object=QueryObjectByFileID(edit.FileID);
    if(!object||object==scene){error="待删除节点已经失效。";return false;}
    if(!firstExecution) {
        Edit current;
        if(!CaptureDeletion(*object,current,error))return false;
        // A later Redo must not erase an unrecorded edit to the target.
        if(current.ParentFileID!=edit.ParentFileID||current.SiblingIndex!=edit.SiblingIndex||current.Subtree!=edit.Subtree) {
            error="删除目标或子树已被其他操作修改，无法重做旧删除。";return false;
        }
    }
    if(!scene->DestroyGameObject(object)){error="Runtime 拒绝删除该节点。";return false;}
    if(SynchronizeDirtyState(error))return true;
    auto* parent=QueryObjectByFileID(edit.ParentFileID);GameSproutRuntime::IGameObject* restored=nullptr;std::string rollback;
    if(!parent||!SceneDocument::RestoreSubtree(edit.Subtree,*Runtime(),*scene,*parent,edit.SiblingIndex,_ObjectIDs,restored,rollback))
        error+=" 恢复删除前子树失败："+rollback;
    return false;
}
bool SceneManager::DeleteObject(uint64_t objectID,std::string& error) {
    if(!Ready(error))return false;
    auto* object=Runtime()->QueryGameObject(objectID);
    if(!object||object==GetActiveScene()||object->GetScene()!=GetActiveScene()){error="不能删除场景根或无效节点。";return false;}
    EditingScope editing(_ExecutingEdit);
    bool success=false;
    try {
        Edit edit;edit.Label="删除节点";
        if(!CaptureDeletion(*object,edit,error))return false;
        _History.reserve(_HistoryPosition+1);
        success=RunDeletion(edit,true,error,true);
        if(success){_History.resize(_HistoryPosition);_History.push_back(std::move(edit));++_HistoryPosition;}
    }catch(const std::exception& exception){error=exception.what();}
    catch(...){error="删除节点时发生异常。";}
    RefreshState(true);return success;
}
