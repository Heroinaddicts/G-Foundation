#include "PhysicsAuthoring.h"
#include "ColliderAuthoringValues.h"
#include "IObjectEditing.h"
#include <QThread>

GameSproutRuntime::IColliderComponent* PhysicsAuthoring::ResolveCollider(const ColliderEditTarget& target) const {
    auto* scene=_Scenes?_Scenes->GetActiveScene():nullptr;
    auto* object=_Runtime?_Runtime->QueryGameObject(target.ObjectID):nullptr;
    if(!scene||target.SceneID!=scene->GetInstanceID()||!object||object==scene||object->GetScene()!=scene)return nullptr;
    for(std::size_t i=0;i<object->GetComponentCount();++i)if(auto* component=object->GetComponentAt(i);
        component&&component->GetComponentInstanceID()==target.ComponentID)return dynamic_cast<GameSproutRuntime::IColliderComponent*>(component);
    return nullptr;
}
bool PhysicsAuthoring::ReadCollider(const ColliderEditTarget& target,ColliderEditState& output,std::string& error) const {
    output={};error.clear();
    if(!_OwnerThread||QThread::currentThread()!=_OwnerThread){error="碰撞体只能在所属线程查询。";return false;}
    auto* collider=ResolveCollider(target);
    if(!collider||!ColliderAuthoringValues::Read(*collider,output)){error="碰撞体已经移除或所属场景已切换。";return false;}
    return true;
}
bool PhysicsAuthoring::CanEditCollider(const ColliderEditTarget& target,std::string& error) const {
    if(!_Objects||!_Scenes||!_Runtime){error="碰撞体作者服务尚未准备好。";return false;}
    if(!_Objects->CanEdit({target.SceneID,target.ObjectID,target.ComponentID},error))return false;
    ColliderEditState value;return ReadCollider(target,value,error);
}
bool PhysicsAuthoring::FitCollider(const ColliderEditTarget& target,std::string& error) {
    if(!CanEditCollider(target,error))return false;
    auto* collider=ResolveCollider(target);ColliderEditState before;
    ColliderAuthoringValues::Read(*collider,before);
    GameSproutRuntime::ColliderFitResult fit;
    if(!collider->CalculateFit(fit,error))return false;
    if(!fit.HasBounds){error="当前对象没有可用于拟合的有效 Mesh；未覆盖碰撞体参数。";return false;}
    auto after=before;after.Settings.Center=fit.Center;
    after.Box=fit.Box;after.Sphere=fit.Sphere;after.Capsule=fit.Capsule;after.Mesh=fit.Mesh;
    if(ColliderAuthoringValues::Equal(before,after))return true;
    const auto file=_Scenes->GetComponentFileID(target.ObjectID,target.ComponentID);
    return _Objects->EditObject({target.SceneID,target.ObjectID,target.ComponentID},"重新拟合碰撞体",
        ColliderAuthoringValues::Operation(_Scenes,file,target.ComponentID,after),
        ColliderAuthoringValues::Operation(_Scenes,file,target.ComponentID,before),error);
}
bool PhysicsAuthoring::HasColliderEdit() const {return _ColliderEdit.Active&&_Objects&&_Objects->HasContinuousEdit();}
bool PhysicsAuthoring::BeginColliderEdit(const ColliderEditTarget& target,std::string& error) {
    if(!CanEditCollider(target,error))return false;
    ColliderEdit candidate;candidate.Active=true;candidate.Target=target;
    if(!ReadCollider(target,candidate.Before,error))return false;
    if(candidate.Before.Kind==ColliderShapeKind::Mesh){error="MeshCollider 的形状由网格决定，不支持基础形状尺寸手柄。";return false;}
    candidate.Last=candidate.Before;candidate.FileID=_Scenes->GetComponentFileID(target.ObjectID,target.ComponentID);
    if(!_Objects->BeginContinuousEdit({target.SceneID,target.ObjectID,target.ComponentID},"编辑碰撞体",
        ColliderAuthoringValues::Operation(_Scenes,candidate.FileID,target.ComponentID,candidate.Before),error))return false;
    _ColliderEdit=std::move(candidate);return true;
}
bool PhysicsAuthoring::PreviewColliderEdit(const ColliderEditState& next,std::string& error) {
    if(!HasColliderEdit()){error="没有活动的碰撞体编辑事务。";return false;}
    ColliderEditState current;
    if(!ReadCollider(_ColliderEdit.Target,current,error))return false;
    if(!ColliderAuthoringValues::Equal(current,_ColliderEdit.Last)) {error="碰撞体已被其他操作修改，请重新开始拖拽。";return false;}
    if(next.Kind!=_ColliderEdit.Before.Kind){error="连续编辑不能更换碰撞体类型。";return false;}
    if(ColliderAuthoringValues::Equal(next,_ColliderEdit.Last))return true;
    if(!_Objects->PreviewContinuousEdit(
        ColliderAuthoringValues::Operation(_Scenes,_ColliderEdit.FileID,_ColliderEdit.Target.ComponentID,next),
        ColliderAuthoringValues::Operation(_Scenes,_ColliderEdit.FileID,_ColliderEdit.Target.ComponentID,_ColliderEdit.Last),error))return false;
    _ColliderEdit.Last=next;return true;
}
bool PhysicsAuthoring::CommitColliderEdit(std::string& error) {
    if(!HasColliderEdit()){_ColliderEdit={};error="没有活动的碰撞体编辑事务。";return false;}
    if(ColliderAuthoringValues::Equal(_ColliderEdit.Before,_ColliderEdit.Last))return CancelColliderEdit(error);
    const auto completed=_ColliderEdit;
    const bool result=_Objects->CommitContinuousEdit(ColliderAuthoringValues::Operation(_Scenes,completed.FileID,
        completed.Target.ComponentID,completed.Last),error);
    if(result||!_Objects->HasContinuousEdit())_ColliderEdit={};
    return result;
}
bool PhysicsAuthoring::CancelColliderEdit(std::string& error) {
    if(!_ColliderEdit.Active){error.clear();return true;}
    const bool result=_Objects?_Objects->CancelContinuousEdit(error):true;
    if(result||!_Objects||!_Objects->HasContinuousEdit())_ColliderEdit={};
    return result;
}
