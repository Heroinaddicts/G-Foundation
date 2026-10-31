#pragma once
#include <cstddef>
#include "Vector4.h"

namespace GameSproutRuntime {
struct Mat4 {
    // 按列连续存储，默认单位矩阵；元素访问使用 (行, 列)，下标范围 [0, 4)。
    // 使用列向量：clip = Projection * View * Model * local。
    float Data[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    constexpr float& operator()(std::size_t row, std::size_t column) { return Data[column * 4 + row]; }
    constexpr float operator()(std::size_t row, std::size_t column) const { return Data[column * 4 + row]; }
    constexpr Mat4 operator*(const Mat4& value) const {
        Mat4 result;
        for (std::size_t column = 0; column < 4; ++column) {
            for (std::size_t row = 0; row < 4; ++row) {
                result(row, column) = 0.0f;
                for (std::size_t k = 0; k < 4; ++k) result(row, column) += (*this)(row, k) * value(k, column);
            }
        }
        return result;
    }
    constexpr Vector4 operator*(Vector4 value) const {
        return {Data[0]*value.X + Data[4]*value.Y + Data[8]*value.Z + Data[12]*value.W,
                Data[1]*value.X + Data[5]*value.Y + Data[9]*value.Z + Data[13]*value.W,
                Data[2]*value.X + Data[6]*value.Y + Data[10]*value.Z + Data[14]*value.W,
                Data[3]*value.X + Data[7]*value.Y + Data[11]*value.Z + Data[15]*value.W};
    }
};
}
