#pragma once
#include "Vector3.h"

namespace GameSproutRuntime {
struct Ray {
    Vector3 Origin;
    Vector3 Direction{0.0f, 0.0f, -1.0f}; // 使用单位方向；distance 的单位与 Origin 一致。
    constexpr Vector3 GetPoint(float distance) const { return Origin + Direction * distance; }
};
}
