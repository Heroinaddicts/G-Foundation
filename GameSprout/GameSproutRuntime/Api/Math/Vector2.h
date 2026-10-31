#pragma once

namespace GameSproutRuntime {
struct Vector2 {
    float X = 0.0f;
    float Y = 0.0f;
    constexpr Vector2() = default;
    constexpr Vector2(float x, float y) : X(x), Y(y) {}
    constexpr Vector2 operator+(Vector2 value) const { return {X + value.X, Y + value.Y}; }
    constexpr Vector2 operator-(Vector2 value) const { return {X - value.X, Y - value.Y}; }
    constexpr Vector2 operator*(float value) const { return {X * value, Y * value}; }
};
}
