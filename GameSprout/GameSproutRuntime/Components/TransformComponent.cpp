#include "TransformComponent.h"
#include "ComponentFactory.h"
#include "RuntimeMath.h"
#include <limits>

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
    : BuiltinComponent<ITransformComponent>(owner, "GameSproutRuntime.Transform") {}

bool TransformComponent::IsSceneRoot() const {
    return _GameObject && static_cast<IGameObject*>(_GameObject->GetScene()) == _GameObject;
}

void TransformComponent::SetLocalPosition(Vector3 value) {
    if (IsSceneRoot() || !RuntimeMath::IsFinite(value) ||
        (value.X == _Position.X && value.Y == _Position.Y && value.Z == _Position.Z)) return;
    _Position = value;
    NotifyComponentChanged(_GameObject, this);
}

bool TransformComponent::SetLocalRotation(Quaternion value) {
    Quaternion unit;
    if (!UnitRotation(value,unit)) return false;
    if (IsSceneRoot()) return unit.X == 0.0f && unit.Y == 0.0f && unit.Z == 0.0f;
    if (unit.X == _Rotation.X && unit.Y == _Rotation.Y && unit.Z == _Rotation.Z && unit.W == _Rotation.W) return true;
    _Rotation = unit;
    NotifyComponentChanged(_GameObject, this);
    return true;
}

void TransformComponent::SetLocalScale(Vector3 value) {
    if (IsSceneRoot() || !RuntimeMath::IsFinite(value) ||
        (value.X == _Scale.X && value.Y == _Scale.Y && value.Z == _Scale.Z)) return;
    _Scale = value;
    NotifyComponentChanged(_GameObject, this);
}

Mat4 TransformComponent::GetLocalMatrix() const {
    return RuntimeMath::ComposeTRS(_Position, _Rotation, _Scale);
}

Mat4 TransformComponent::GetWorldMatrix() const {
    Mat4 world = GetLocalMatrix();
    // 迭代沿父链累积，避免深层树递归消耗调用栈；节点管理器负责拒绝父子循环。
    for (IGameObject* parent = _GameObject->Parent(); parent; parent = parent->Parent()) {
        if (ITransformComponent* transform = parent->GetTransform()) world = transform->GetLocalMatrix() * world;
    }
    return world;
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
    NotifyComponentChanged(_GameObject, this);
    return true;
}
}
