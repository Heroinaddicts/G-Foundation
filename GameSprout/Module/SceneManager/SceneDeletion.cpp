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
    Json::Value document;std::string text,error;
    if(!Capture(document,text,error))return 0;
    found=_ObjectIDs.RuntimeToFile.find(objectID);
    return found==_ObjectIDs.RuntimeToFile.end()?0:found->second;
}
GameSproutRuntime::IGameObject* SceneManager::QueryObjectByFileID(uint64_t fileID) const {
    auto* scene=GetActiveScene();if(!scene)return nullptr;
    if(!fileID)return scene;
    for(const auto& entry:_ObjectIDs.RuntimeToFile) if(entry.second==fileID) {
        auto* object=Runtime()->QueryGameObject(entry.first);
        return object&&object->GetScene()==scene?object:nullptr;
    }
    return nullptr;
}
bool SceneManager::CaptureDeletion(GameSproutRuntime::IGameObject& object,Edit& edit,std::string& error) const {
    auto* scene=GetActiveScene();auto* parent=object.Parent();
    if(!scene||&object==scene||object.GetScene()!=scene||!parent){error="不能删除场景根或无效节点。";return false;}
    Json::Value document;std::string text;
    if(!Capture(document,text,error))return false;
    edit.Type=Edit::Kind::Deletion;edit.FileID=_ObjectIDs.RuntimeToFile.at(object.GetInstanceID());
    edit.ParentFileID=parent==scene?0:_ObjectIDs.RuntimeToFile.at(parent->GetInstanceID());
    edit.SiblingIndex=parent->GetChildCount();
    for(std::size_t index=0;index<parent->GetChildCount();++index)if(parent->GetChild(index)==&object){edit.SiblingIndex=index;break;}
    if(edit.SiblingIndex==parent->GetChildCount()){error="节点不在父级列表中。";return false;}
    std::vector<GameSproutRuntime::IGameObject*> pending{&object};std::set<uint64_t> files;
    while(!pending.empty()) {
        auto* node=pending.back();pending.pop_back();files.insert(_ObjectIDs.RuntimeToFile.at(node->GetInstanceID()));
        for(std::size_t index=node->GetChildCount();index>0;--index)pending.push_back(node->GetChild(index-1));
    }
    Json::Value subtree(Json::objectValue);
    subtree["Version"]=Json::UInt(1);subtree["Name"]="Deleted subtree";
    const auto camera=document["MainCameraID"].asUInt64();
    subtree["MainCameraID"]=Json::UInt64(files.count(camera)?camera:0);
    subtree["Objects"]=Json::Value(Json::arrayValue);
    for(const auto& node:document["Objects"]) if(files.count(node["ID"].asUInt64())) {
        Json::Value copy=node;
        if(copy["ID"].asUInt64()==edit.FileID)copy["ParentID"]=Json::UInt64(0);
        subtree["Objects"].append(std::move(copy));
    }
    if(!SceneDocument::ValidateSubtree(subtree,error))return false;
    edit.AnimationBindings=SceneAnimationDocument::CaptureBindings(document);
    edit.Subtree.swap(subtree);return true;
}
bool SceneManager::RunDeletion(const Edit& edit,bool forward,std::string& error) {
    auto* scene=GetActiveScene();if(!scene){error="当前场景已失效。";return false;}
    if(!forward) {
        if(QueryObjectByFileID(edit.FileID)){error="待恢复节点的稳定身份已经存在。";return false;}
        auto* parent=QueryObjectByFileID(edit.ParentFileID);
        if(!parent){error="待恢复子树的父节点已经删除。";return false;}
        GameSproutRuntime::IGameObject* restored=nullptr;
        if(!SceneDocument::RestoreSubtree(edit.Subtree,*Runtime(),*scene,*parent,edit.SiblingIndex,_ObjectIDs,restored,error))return false;
        SceneAnimationDocument::RestoreBindings(edit.AnimationBindings,*Runtime(),*scene,_ObjectIDs);
        return true;
    }
    auto* object=QueryObjectByFileID(edit.FileID);
    if(!object||object==scene){error="待删除节点已经失效。";return false;}
    Edit current;
    if(!CaptureDeletion(*object,current,error))return false;
    // 未通过历史登记的外部修改不能被旧Redo快照静默丢掉；用户可重新执行Delete登记新快照。
    if(current.ParentFileID!=edit.ParentFileID||current.SiblingIndex!=edit.SiblingIndex||current.Subtree!=edit.Subtree) {
        error="删除目标或子树已被其他操作修改，无法重做旧删除。";return false;
    }
    if(!scene->DestroyGameObject(object)){error="Runtime 拒绝删除该节点。";return false;}
    Json::Value document;std::string text;
    if(Capture(document,text,error))return true;
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
        success=RunDeletion(edit,true,error);
        if(success){_History.resize(_HistoryPosition);_History.push_back(std::move(edit));++_HistoryPosition;}
    }catch(const std::exception& exception){error=exception.what();}
    catch(...){error="删除节点时发生异常。";}
    RefreshState(true);return success;
}
