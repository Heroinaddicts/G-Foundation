#include "TransformComponent.h"
#include "ComponentFactory.h"
#include "RuntimeMath.h"
#include "GameObject.h"
#include <limits>
#include <vector>

namespace {
bool UnitRotation(GameSproutRuntime::Quaternion value,GameSproutRuntime::Quaternion& unit) {
    const double squared=double(value.X)*value.X+double(value.Y)*value.Y+double(value.Z)*value.Z+double(value.W)*value.W;
    // Float单位四元数本身有舍入误差。保留此范围内的分量，使getter值写回/撤销恢复精确幂等。
    if(std::isfinite(squared) && std::fabs(squared-1.0)<=8.0*std::numeric_limits<float>::epsilon()) unit=value;
    else if(!value.TryNormalize(unit)) return false;
    // q/-q规范为同一表示，包括W==0的180度旋转。
    if(unit.W<0 || (unit.W==0 && (unit.Z<0 || (unit.Z==0 && (unit.Y<0 || (unit.Y==0 && unit.X<0))))))
        unit={-unit.X,-unit.Y,-unit.Z,-unit.W};
    return true;
}
}

namespace GameSproutRuntime {
TransformComponent::TransformComponent(IGameObject* owner)
    : BuiltinComponent<ITransformComponent>(owner, "GameSproutRuntime.Transform") {
    if(auto* access=dynamic_cast<ObjectAccess*>(owner))_Runtime=&access->State().Owner;
}

void TransformComponent::InvalidateLocalMatrix() {
    _LocalMatrixKnown=false;
    if(_Runtime)_Runtime->InvalidateTransforms();
}

bool TransformComponent::IsSceneRoot() const {
    return _GameObject && static_cast<IGameObject*>(_GameObject->GetScene()) == _GameObject;
}

void TransformComponent::SetLocalPosition(Vector3 value) {
    if (IsSceneRoot() || !RuntimeMath::IsFinite(value) ||
        (value.X == GetLocalPosition().X && value.Y == GetLocalPosition().Y && value.Z == GetLocalPosition().Z)) return;
    PrepareAuthoringChange();
    _Position = value;
    InvalidateLocalMatrix();
    ++_AuthoringRevision;
    NotifyComponentChanged(_GameObject, this);
}

bool TransformComponent::SetLocalRotation(Quaternion value) {
    Quaternion unit;
    if (!UnitRotation(value,unit)) return false;
    if (IsSceneRoot()) return unit.X == 0.0f && unit.Y == 0.0f && unit.Z == 0.0f;
    const auto current=GetLocalRotation();
    if (unit.X == current.X && unit.Y == current.Y && unit.Z == current.Z && unit.W == current.W) return true;
    PrepareAuthoringChange();
    _Rotation = unit;
    InvalidateLocalMatrix();
    ++_AuthoringRevision;
    NotifyComponentChanged(_GameObject, this);
    return true;
}

void TransformComponent::SetLocalScale(Vector3 value) {
    if (IsSceneRoot() || !RuntimeMath::IsFinite(value) ||
        (value.X == _Scale.X && value.Y == _Scale.Y && value.Z == _Scale.Z)) return;
    PrepareAuthoringChange();
    _Scale = value;
    InvalidateLocalMatrix();
    ++_AuthoringRevision;
    NotifyComponentChanged(_GameObject, this);
}

Mat4 TransformComponent::GetLocalMatrix() const {
    if(!_LocalMatrixKnown) {
        if(_HasPhysicsPose&&_PhysicsDynamic)_LocalMatrixCache=_PhysicsMatrix;
        else if(_PoseOwner)_LocalMatrixCache=_EvaluatedMatrix;
        else if(_HasPhysicsPose)_LocalMatrixCache=_PhysicsMatrix;
        else _LocalMatrixCache=RuntimeMath::ComposeTRS(_Position,_Rotation,_Scale);
        _LocalMatrixKnown=true;
    }
    return _LocalMatrixCache;
}

bool TransformComponent::SetEvaluatedPose(std::uint64_t animatorID,const Mat4& matrix) {
    if(!animatorID || IsSceneRoot() || (_HasPhysicsPose&&_PhysicsDynamic) || !RuntimeMath::IsFinite(matrix) || (_PoseOwner && _PoseOwner!=animatorID)) return false;
    bool changed=_PoseOwner!=animatorID;
    for(unsigned index=0;index<16&&!changed;++index)changed=_EvaluatedMatrix.Data[index]!=matrix.Data[index];
    _EvaluatedMatrix=matrix;_PoseOwner=animatorID;
    if(changed){InvalidateLocalMatrix();NotifyRuntimePropertiesChanged(_GameObject);}
    return true;
}
void TransformComponent::ClearEvaluatedPose(std::uint64_t animatorID) {if(_PoseOwner==animatorID){_PoseOwner=0;InvalidateLocalMatrix();NotifyRuntimePropertiesChanged(_GameObject);}}

Mat4 TransformComponent::GetWorldMatrix() const {
    if(!_Runtime) {
        Mat4 world=GetLocalMatrix();
        for(IGameObject* parent=_GameObject->Parent();parent;parent=parent->Parent())
            if(auto* transform=parent->GetTransform())world=transform->GetLocalMatrix()*world;
        return world;
    }
    const auto epoch=_Runtime->GetTransformEpoch();
    if(_WorldMatrixEpoch==epoch)return _WorldMatrixCache;
    // Resolve a cold/changed ancestor chain iteratively. Cache every visited
    // parent, so a scene traversal computes each unchanged world matrix once.
    std::vector<const TransformComponent*> chain;
    const TransformComponent* current=this;
    Mat4 world;
    while(current) {
        if(current->_WorldMatrixEpoch==epoch){world=current->_WorldMatrixCache;break;}
        chain.push_back(current);
        auto* parent=current->_GameObject->Parent();
        auto* parentTransform=parent?parent->GetTransform():nullptr;
        current=dynamic_cast<const TransformComponent*>(parentTransform);
        if(parentTransform&&!current){world=parentTransform->GetWorldMatrix();break;}
    }
    for(auto item=chain.rbegin();item!=chain.rend();++item) {
        world=world*(*item)->GetLocalMatrix();
        (*item)->_WorldMatrixCache=world;(*item)->_WorldMatrixEpoch=epoch;
    }
    return _WorldMatrixCache;
}

bool TransformComponent::SetLocalMatrix(const Mat4& value) {
    if (IsSceneRoot()) return false;
    Vector3 position, scale;
    Quaternion rotation;
    if (!RuntimeMath::TryDecomposeTRS(value, position, rotation, scale)) return false;
    Quaternion unit;
    if(!UnitRotation(rotation,unit)) return false;
    _Position = position;
    _Rotation = unit;
    _Scale = scale;
    _HasPhysicsPose=false;
    InvalidateLocalMatrix();
    ++_AuthoringRevision;
    NotifyComponentChanged(_GameObject, this);
    return true;
}
void TransformComponent::PrepareAuthoringChange() {
    if(!_HasPhysicsPose)return;
    _Position=_PhysicsPosition;
    _Rotation=_PhysicsRotation;
    _HasPhysicsPose=false;
}
bool TransformComponent::SetPhysicsPose(const Mat4& local,bool dynamic) {
    Vector3 position,scale;Quaternion rotation;
    if(IsSceneRoot()||!RuntimeMath::TryDecomposeTRS(local,position,rotation,scale))return false;
    bool changed=!_HasPhysicsPose||_PhysicsDynamic!=dynamic;
    for(unsigned index=0;index<16&&!changed;++index)changed=_PhysicsMatrix.Data[index]!=local.Data[index];
    _PhysicsPosition=position;_PhysicsRotation=rotation;_PhysicsMatrix=local;
    _HasPhysicsPose=true;_PhysicsDynamic=dynamic;
    if(changed){InvalidateLocalMatrix();NotifyRuntimePropertiesChanged(_GameObject);}
    return true;
}
void TransformComponent::ClearPhysicsPose(){
    const bool changed=_HasPhysicsPose;
    _HasPhysicsPose=false;_PhysicsDynamic=false;
    if(changed){InvalidateLocalMatrix();NotifyRuntimePropertiesChanged(_GameObject);}
}
}
