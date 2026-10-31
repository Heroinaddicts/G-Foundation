#include "CameraComponent.h"
#include "ComponentFactory.h"
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
    return transform && RuntimeMath::TryInverseAffine(transform->GetWorldMatrix(), output);
}
bool CameraComponent::GetProjectionMatrix(float aspect, Mat4& output) const {
    if (_Settings.Projection == CameraProjection::Perspective) {
        return TryMakePerspective(_Settings.VerticalFovRadians, aspect, _Settings.NearPlane, _Settings.FarPlane, output);
    }
    return TryMakeOrthographic(_Settings.OrthographicHeight, aspect, _Settings.NearPlane, _Settings.FarPlane, output);
}
}
