#pragma once
#include "Vector3.h"

namespace GameSproutRuntime {
struct Bounds {
    Vector3 Center;
    Vector3 Extents; // 轴对齐包围盒的非负半尺寸，不是完整宽高深。
    constexpr Vector3 Min() const { return Center - Extents; }
    constexpr Vector3 Max() const { return Center + Extents; }
};
}
