#pragma once
#include <cmath>

namespace GameSproutRuntime {
struct Vector3 {
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    constexpr Vector3() = default;
    constexpr Vector3(float x, float y, float z) : X(x), Y(y), Z(z) {}
    constexpr Vector3 operator+(Vector3 value) const { return {X + value.X, Y + value.Y, Z + value.Z}; }
    constexpr Vector3 operator-(Vector3 value) const { return {X - value.X, Y - value.Y, Z - value.Z}; }
    constexpr Vector3 operator-() const { return {-X, -Y, -Z}; }
    constexpr Vector3 operator*(float value) const { return {X * value, Y * value, Z * value}; }
    constexpr float Dot(Vector3 value) const { return X * value.X + Y * value.Y + Z * value.Z; }
    constexpr Vector3 Cross(Vector3 value) const {
        return {Y * value.Z - Z * value.Y, Z * value.X - X * value.Z, X * value.Y - Y * value.X};
    }
    float Length() const { return std::sqrt(Dot(*this)); }
    // 零向量或非有限值无法归一化；失败时不修改输出。
    bool TryNormalize(Vector3& output) const {
        const float largest = std::fmax(std::fabs(X), std::fmax(std::fabs(Y), std::fabs(Z)));
        if (!std::isfinite(X) || !std::isfinite(Y) || !std::isfinite(Z) || largest == 0.0f) return false;
        const Vector3 scaled{X / largest, Y / largest, Z / largest};
        output = scaled * (1.0f / std::sqrt(scaled.Dot(scaled)));
        return true;
    }
};
}
