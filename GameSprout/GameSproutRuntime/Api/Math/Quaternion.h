#pragma once
#include <cmath>

namespace GameSproutRuntime {
struct Quaternion {
    // XYZW 顺序，默认无旋转；有效旋转使用单位四元数。
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float W = 1.0f;
    constexpr Quaternion() = default;
    constexpr Quaternion(float x, float y, float z, float w) : X(x), Y(y), Z(z), W(w) {}
    bool TryNormalize(Quaternion& output) const {
        const float largest = std::fmax(std::fmax(std::fabs(X), std::fabs(Y)), std::fmax(std::fabs(Z), std::fabs(W)));
        if (!std::isfinite(X) || !std::isfinite(Y) || !std::isfinite(Z) || !std::isfinite(W) || largest == 0.0f) return false;
        const float x = X / largest, y = Y / largest, z = Z / largest, w = W / largest;
        const float inverseLength = 1.0f / std::sqrt(x*x + y*y + z*z + w*w);
        output = {x*inverseLength, y*inverseLength, z*inverseLength, w*inverseLength};
        return true;
    }
};
}
