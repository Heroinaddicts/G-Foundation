#pragma once

namespace GameSproutRuntime {
struct Vector4 {
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float W = 0.0f;
    constexpr Vector4() = default;
    constexpr Vector4(float x, float y, float z, float w) : X(x), Y(y), Z(z), W(w) {}
    constexpr Vector4 operator+(Vector4 value) const { return {X + value.X, Y + value.Y, Z + value.Z, W + value.W}; }
    constexpr Vector4 operator*(float value) const { return {X * value, Y * value, Z * value, W * value}; }
};
}
