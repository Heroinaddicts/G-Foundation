#pragma once
#include <cstddef>
#include "Vector3.h"

namespace GameSproutRuntime {
struct Mat3 {
    // 按列连续存储，默认单位矩阵；元素访问使用 (行, 列)，下标范围 [0, 3)。
    float Data[9] = {1,0,0, 0,1,0, 0,0,1};
    constexpr float& operator()(std::size_t row, std::size_t column) { return Data[column * 3 + row]; }
    constexpr float operator()(std::size_t row, std::size_t column) const { return Data[column * 3 + row]; }
    constexpr Vector3 operator*(Vector3 value) const {
        return {Data[0]*value.X + Data[3]*value.Y + Data[6]*value.Z,
                Data[1]*value.X + Data[4]*value.Y + Data[7]*value.Z,
                Data[2]*value.X + Data[5]*value.Y + Data[8]*value.Z};
    }
};
}
