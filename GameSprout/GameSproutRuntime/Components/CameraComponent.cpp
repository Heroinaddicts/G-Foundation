#include "CameraComponent.h"
#include "ComponentFactory.h"
#include "TransformComponent.h"
#include "RuntimeMath.h"
#include <cmath>

namespace GameSproutRuntime {
CameraComponent::CameraComponent(IGameObject* owner)
    : BuiltinComponent<ICameraComponent>(owner, "GameSproutRuntime.Camera") {}
bool CameraComponent::SetSettings(const CameraSettings& value) {
    if ((value.Projection != CameraProjection::Perspective && value.Projection != CameraProjection::Orthographic) ||
        !std::isfinite(value.VerticalFovRadians) || value.VerticalFovRadians <= 0 || value.VerticalFovRadians >= Pi ||
        !std::isfinite(value.OrthographicHeight) || value.OrthographicHeight <= 0 ||
        !std::isfinite(value.NearPlane) || value.NearPlane < 0 ||
        (value.Projection == CameraProjection::Perspective && value.NearPlane == 0) ||
        !std::isfinite(value.FarPlane) || value.FarPlane <= value.NearPlane) return false;
    if (value.Projection == _Settings.Projection && value.VerticalFovRadians == _Settings.VerticalFovRadians &&
        value.OrthographicHeight == _Settings.OrthographicHeight && value.NearPlane == _Settings.NearPlane &&
        value.FarPlane == _Settings.FarPlane && value.CullingMask == _Settings.CullingMask) return true;
    _Settings = value;
    NotifyComponentChanged(_GameObject, this);
    return true;
}
bool CameraComponent::GetViewMatrix(Mat4& output) const {
    ITransformComponent* transform = _GameObject->GetTransform();
    if (!transform) return false;
    // Unity Camera::GetImplicitWorldToCameraMatrix uses world position and
    // rotation without scale, then reflects camera-space Z exactly once.
    const Mat4 world=transform->GetWorldMatrix();
    if (!RuntimeMath::IsFinite(world)) return false;
    Mat4 pose;
    for (auto* node=_GameObject;node;node=node->Parent()) {
        auto* local=node->GetTransform();
        if (!local) return false;
        Quaternion rotation;
        if (const auto* current=dynamic_cast<const TransformComponent*>(local)) {
            if (!current->GetCurrentLocalRotation(rotation)) return false;
        } else rotation=local->GetLocalRotation();
        pose=RuntimeMath::ComposeTRS({},rotation,{1,1,1})*pose;
    }
    for (std::size_t row=0;row<3;++row) pose(row,3)=world(row,3);
    Mat4 view;
    if (!RuntimeMath::TryInverseAffine(pose,view)) return false;
    for (std::size_t column=0;column<4;++column) view(2,column)=-view(2,column);
    output=view;
    return true;
}
bool CameraComponent::GetProjectionMatrix(float aspect, Mat4& output) const {
    if (_Settings.Projection == CameraProjection::Perspective) {
        return TryMakePerspective(_Settings.VerticalFovRadians, aspect, _Settings.NearPlane, _Settings.FarPlane, output);
    }
    return TryMakeOrthographic(_Settings.OrthographicHeight, aspect, _Settings.NearPlane, _Settings.FarPlane, output);
}
}
