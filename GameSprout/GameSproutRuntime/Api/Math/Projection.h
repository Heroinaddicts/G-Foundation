#pragma once
#include <cmath>
#include "CoordinateSystem.h"

namespace GameSproutRuntime {
// 标准投影：右手系、前方 -Z、NDC X/Y 为 [-1,1]，深度为 [0,1]。
// 失败不修改输出。宽高比使用实际渲染目标像素宽 / 高。
inline bool TryMakePerspective(float verticalFovRadians, float aspect, float nearPlane, float farPlane, Mat4& output) {
    if (!std::isfinite(verticalFovRadians) || verticalFovRadians <= 0 || verticalFovRadians >= Pi ||
        !std::isfinite(aspect) || aspect <= 0 || !std::isfinite(nearPlane) || nearPlane <= 0 ||
        !std::isfinite(farPlane) || farPlane <= nearPlane) return false;
    const double y = 1.0 / std::tan(static_cast<double>(verticalFovRadians) * 0.5);
    const double z = static_cast<double>(farPlane) / (static_cast<double>(nearPlane) - farPlane);
    Mat4 result;
    for (float& value : result.Data) value = 0.0f;
    result(0,0) = static_cast<float>(y / aspect);
    result(1,1) = static_cast<float>(y);
    result(2,2) = static_cast<float>(z);
    result(2,3) = static_cast<float>(z * nearPlane);
    result(3,2) = -1.0f;
    if (result(0,0) <= 0.0f || result(1,1) <= 0.0f) return false;
    for (float value : result.Data) if (!std::isfinite(value)) return false;
    output = result;
    return true;
}

inline bool TryMakeOrthographic(float height, float aspect, float nearPlane, float farPlane, Mat4& output) {
    if (!std::isfinite(height) || height <= 0 || !std::isfinite(aspect) || aspect <= 0 ||
        !std::isfinite(nearPlane) || nearPlane < 0 || !std::isfinite(farPlane) || farPlane <= nearPlane) return false;
    Mat4 result;
    result(0,0) = static_cast<float>(2.0 / (static_cast<double>(height) * aspect));
    result(1,1) = static_cast<float>(2.0 / height);
    result(2,2) = static_cast<float>(1.0 / (static_cast<double>(nearPlane) - farPlane));
    result(2,3) = static_cast<float>(nearPlane / (static_cast<double>(nearPlane) - farPlane));
    if (result(0,0) <= 0.0f || result(1,1) <= 0.0f || result(2,2) == 0.0f) return false;
    for (float value : result.Data) if (!std::isfinite(value)) return false;
    output = result;
    return true;
}

// 相机世界位置与观察目标。两者不能重合，up 不能平行于观察方向。
inline bool TryMakeLookAt(Vector3 eye, Vector3 target, Vector3 worldUp, Mat4& output) {
    Vector3 forward, up, right;
    if (!(target - eye).TryNormalize(forward) || !worldUp.TryNormalize(up) ||
        !forward.Cross(up).TryNormalize(right)) return false;
    up = right.Cross(forward);
    Mat4 result;
    result(0,0)=right.X; result(0,1)=right.Y; result(0,2)=right.Z; result(0,3)=-right.Dot(eye);
    result(1,0)=up.X; result(1,1)=up.Y; result(1,2)=up.Z; result(1,3)=-up.Dot(eye);
    result(2,0)=-forward.X; result(2,1)=-forward.Y; result(2,2)=-forward.Z; result(2,3)=forward.Dot(eye);
    for (float value : result.Data) if (!std::isfinite(value)) return false;
    output = result;
    return true;
}

enum class ClipDepthRange { ZeroToOne, MinusOneToOne };
// 后端适配按实际契约调用一次。Y 翻转若改变图元绕序，剔除设置也要对应调整。
inline Mat4 AdaptProjection(const Mat4& canonicalProjection, ClipDepthRange depth, bool flipY) {
    Mat4 result = canonicalProjection;
    for (std::size_t column = 0; column < 4; ++column) {
        if (depth == ClipDepthRange::MinusOneToOne) result(2,column) = 2.0f * canonicalProjection(2,column) - canonicalProjection(3,column);
        if (flipY) result(1,column) = -canonicalProjection(1,column);
    }
    return result;
}
}
