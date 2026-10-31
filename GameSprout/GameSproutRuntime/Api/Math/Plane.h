#pragma once
#include "Vector3.h"

namespace GameSproutRuntime {
struct Plane {
    // 平面方程 Dot(Normal, point) + Distance = 0；Normal 应为单位向量。
    Vector3 Normal{0.0f, 1.0f, 0.0f};
    float Distance = 0.0f;
    constexpr float SignedDistance(Vector3 point) const { return Normal.Dot(point) + Distance; }
};
}
