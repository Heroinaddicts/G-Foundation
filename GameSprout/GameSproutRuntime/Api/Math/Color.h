#pragma once

namespace GameSproutRuntime {
struct Color {
    // 线性 RGB，Alpha 不透明度；默认白色。HDR RGB 可超过 1。
    float R = 1.0f;
    float G = 1.0f;
    float B = 1.0f;
    float A = 1.0f;
    constexpr Color() = default;
    constexpr Color(float r, float g, float b, float a = 1.0f) : R(r), G(g), B(b), A(a) {}
};
}
