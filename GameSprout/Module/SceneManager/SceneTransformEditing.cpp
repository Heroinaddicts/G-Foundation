#include "SceneManager.h"
#include "Math/TransformMath.h"
#include <exception>

namespace {
class EditingScope {
public:
    explicit EditingScope(bool& flag):_Flag(flag) {_Flag=true;}
    ~EditingScope() {_Flag=false;}
private:
    bool& _Flag;
};
SceneTransformState Read(const GameSproutRuntime::ITransformComponent& transform) {
    return {transform.GetLocalPosition(),transform.GetLocalRotation(),transform.GetLocalScale()};
}
bool ValidRotation(const GameSproutRuntime::Quaternion& value) {
    return std::isfinite(value.X)&&std::isfinite(value.Y)&&std::isfinite(value.Z)&&std::isfinite(value.W)
        &&(value.X!=0||value.Y!=0||value.Z!=0||value.W!=0);
}
bool SameRotation(const GameSproutRuntime::Quaternion& a,const GameSproutRuntime::Quaternion& b) {
    if(!ValidRotation(a)||!ValidRotation(b))return false;
    const double first=double(a.X)*a.X+double(a.Y)*a.Y+double(a.Z)*a.Z+double(a.W)*a.W;
    const double second=double(b.X)*b.X+double(b.Y)*b.Y+double(b.Z)*b.Z+double(b.W)*b.W;
    const double dot=double(a.X)*b.X+double(a.Y)*b.Y+double(a.Z)*b.Z+double(a.W)*b.W;
    // setter可以归一化并规范q/-q；不用exact比较归一化后的float分量。
    return std::fabs(dot/std::sqrt(first*second))>=1.0-1e-12;
}
bool Equal(const SceneTransformState& a,const SceneTransformState& b) {
    return SameRotation(a.Rotation,b.Rotation)&&a.Position.X==b.Position.X&&a.Position.Y==b.Position.Y&&a.Position.Z==b.Position.Z
        &&a.Scale.X==b.Scale.X&&a.Scale.Y==b.Scale.Y&&a.Scale.Z==b.Scale.Z;
}
bool Write(GameSproutRuntime::IGameObject& object,const SceneTransformState& value,std::string& error) {
    if(!GameSproutRuntime::RuntimeMath::IsFinite(value.Position)||!GameSproutRuntime::RuntimeMath::IsFinite(value.Scale)
        ||!ValidRotation(value.Rotation)) {error="变换包含非有限值或无效旋转。";return false;}
    auto* transform=object.GetTransform();
    // 仅Runtime进行归一化，避免先归一化再传入setter造成两次舍入。
    if(!transform->SetLocalRotation(value.Rotation)) {error="Runtime 拒绝该旋转。";return false;}
    transform->SetLocalPosition(value.Position);transform->SetLocalScale(value.Scale);
    if(!Equal(Read(*transform),value)){error="Runtime 拒绝该变换。";return false;}
    return true;
}
}

GameSproutRuntime::IGameObject* SceneManager::TransformTarget(bool checkParent) const {
    if(!_TransformEdit.Active||!_Runtime||_ActiveID!=_TransformEdit.SceneID)return nullptr;
    auto* scene=GetActiveScene();auto* object=_Runtime->QueryGameObject(_TransformEdit.ObjectID);
    if(!scene||!object||object==scene||object->GetScene()!=scene)return nullptr;
    if(checkParent) {
        auto* parent=object->Parent();
        if((parent?parent->GetInstanceID():0)!=_TransformEdit.ParentID)return nullptr;
        const auto matrix=parent?parent->GetTransform()->GetWorldMatrix():GameSproutRuntime::Mat4{};
        for(int index=0;index<16;++index)if(matrix.Data[index]!=_TransformEdit.ParentWorld.Data[index])return nullptr;
        if(!Equal(Read(*object->GetTransform()),_TransformEdit.Last))return nullptr;
    }
    return object;
}
bool SceneManager::BeginTransformEdit(uint64_t objectID,const std::string& label,std::string& error) {
    if(!Ready(error))return false;
    auto* scene=GetActiveScene();auto* object=_Runtime->QueryGameObject(objectID);
    if(!object||object==scene||object->GetScene()!=scene){error="不能变换无效节点或场景根。";return false;}
    try {
        // 拖拽不能以无法序列化/世界矩阵已经溢出的场景作为起点。
        Json::Value document;std::string text;
        if(!Capture(document,text,error))return false;
        TransformEdit edit;edit.Active=true;edit.SceneID=_ActiveID;edit.ObjectID=objectID;edit.Label=label;
        edit.Before=edit.Last=Read(*object->GetTransform());
        if(auto* parent=object->Parent()){edit.ParentID=parent->GetInstanceID();edit.ParentWorld=parent->GetTransform()->GetWorldMatrix();}
        _TransformEdit=std::move(edit);return true;
    } catch(const std::exception& exception) {error=exception.what();return false;}
    catch(...) {error="无法开始连续变换。";return false;}
}
bool SceneManager::PreviewTransformEdit(const SceneTransformState& value,std::string& error) {
    error.clear();
    if(!_TransformEdit.Active||!_Runtime||_Runtime->IsExecuting()||_Switching||_ExecutingEdit||_Notifying){error="连续变换当前不可用。";return false;}
    auto* object=TransformTarget(true);
    if(!object){std::string ignored;CancelTransformEdit(ignored);error="目标、父节点或变换已改变，拖拽已取消。";return false;}
    EditingScope editing(_ExecutingEdit);
    bool accepted=false;
    try {
        accepted=Write(*object,value,error);
        if(accepted) {
            Json::Value document;std::string text;
            accepted=Capture(document,text,error);
        }
        if(accepted)_TransformEdit.Last=Read(*object->GetTransform());
    } catch(const std::exception& exception) {accepted=false;error=exception.what();}
    catch(...) {accepted=false;error="预览变换时发生异常。";}
    if(!accepted) {
        std::string rollback;
        try {if(!Write(*object,_TransformEdit.Last,rollback))error+=" 恢复上次预览失败："+rollback;}
        catch(...) {error+=" 恢复上次预览时发生异常。";}
    }
    // 同步通知Inspector；监听回调不能重入取消、保存或其他编辑。
    RefreshState(true);return accepted;
}
bool SceneManager::CancelTransformEdit(std::string& error) {
    error.clear();if(!_TransformEdit.Active)return true;
    if(!_Runtime||_Runtime->IsExecuting()||_Switching||_ExecutingEdit||_Notifying){error="当前操作中不能取消变换。";return false;}
    auto* object=TransformTarget(false);const auto before=_TransformEdit.Before;
    _TransformEdit={};
    EditingScope editing(_ExecutingEdit);
    bool restored=!object;
    try {if(object)restored=Write(*object,before,error);}
    catch(const std::exception& exception) {error=exception.what();}
    catch(...) {error="取消变换时发生异常。";}
    if(GetActiveScene())RefreshState(true);
    return restored;
}
bool SceneManager::CommitTransformEdit(std::string& error) {
    error.clear();
    if(!_TransformEdit.Active){error="没有活动的连续变换。";return false;}
    if(!_Runtime||_Runtime->IsExecuting()||_Switching||_ExecutingEdit||_Notifying){error="当前不能提交变换。";return false;}
    auto* object=TransformTarget(true);
    if(!object){std::string ignored;CancelTransformEdit(ignored);error="变换目标已经失效。";return false;}
    const auto before=_TransformEdit.Before,after=Read(*object->GetTransform());
    // 等价旋转可能有不同float表示；零变化也恢复精确起始值，使保存快照保持干净。
    if(Equal(before,after))return CancelTransformEdit(error);
    const auto id=_TransformEdit.ObjectID,sceneID=_TransformEdit.SceneID;
    std::string label;
    try {label=_TransformEdit.Label;}catch(...) {error="无法分配撤销记录。";return false;}
    _TransformEdit={};
    bool committed=false;
    try {
        committed=EditObject(id,label,[after](auto& target,std::string& failure){return Write(target,after,failure);},
            [before](auto& target,std::string& failure){return Write(target,before,failure);},error);
    } catch(const std::exception& exception) {error=exception.what();}
    catch(...) {error="记录拖拽变换时发生异常。";}
    if(!committed && _ActiveID==sceneID) {
        // 历史分配也可能在执行apply之前失败，此时仍需撤回已显示的Preview。
        auto* current=_Runtime->QueryGameObject(id);
        if(current&&current->GetScene()==GetActiveScene()) {
            EditingScope editing(_ExecutingEdit);std::string rollback;
            try {if(!Write(*current,before,rollback))error+=" 恢复拖拽前变换失败："+rollback;}
            catch(...) {error+=" 恢复拖拽前变换时发生异常。";}
            RefreshState(true);
        }
    }
    return committed;
}
