#pragma once
#include <cmath>
#include "Mat3.h"
#include "Mat4.h"
#include "Quaternion.h"
#include "Bounds.h"
#include "Plane.h"
#include "Ray.h"

namespace GameSproutRuntime {
enum class Handedness { RightHanded, LeftHanded };
enum class Axis { PositiveX, NegativeX, PositiveY, NegativeY, PositiveZ, NegativeZ };

struct CoordinateSystem {
    Handedness Hand = Handedness::RightHanded;
    Axis Up = Axis::PositiveY;
    Axis Forward = Axis::NegativeZ;
    float MetersPerUnit = 1.0f;
};

// 世界和局部空间统一采用此约定。相机局部前方 -Z，正旋转服从右手规则。
inline constexpr CoordinateSystem RuntimeCoordinates{};
inline constexpr float Pi = 3.14159265358979323846f;
constexpr float DegreesToRadians(float value) { return value * (Pi / 180.0f); }
constexpr float RadiansToDegrees(float value) { return value * (180.0f / Pi); }

namespace CoordinateDetail {
inline bool GetAxis(Axis axis, Vector3& output) {
    switch (axis) {
    case Axis::PositiveX: output = {1,0,0}; return true;
    case Axis::NegativeX: output = {-1,0,0}; return true;
    case Axis::PositiveY: output = {0,1,0}; return true;
    case Axis::NegativeY: output = {0,-1,0}; return true;
    case Axis::PositiveZ: output = {0,0,1}; return true;
    case Axis::NegativeZ: output = {0,0,-1}; return true;
    }
    return false;
}
inline bool GetBasis(const CoordinateSystem& system, Vector3 (&basis)[3]) {
    Vector3 up, forward;
    if (!std::isfinite(system.MetersPerUnit) || system.MetersPerUnit <= 0.0f ||
        !GetAxis(system.Up, up) || !GetAxis(system.Forward, forward) || up.Dot(forward) != 0.0f) return false;
    if (system.Hand != Handedness::RightHanded && system.Hand != Handedness::LeftHanded) return false;
    basis[0] = system.Hand == Handedness::RightHanded ? forward.Cross(up) : up.Cross(forward);
    basis[1] = up;
    basis[2] = -forward;
    return true;
}
inline float Element(Vector3 value, std::size_t row) { return row == 0 ? value.X : row == 1 ? value.Y : value.Z; }
}

// 在两个轴正交、原点重合的坐标约定间换基。不是父子 Transform 的空间转换。
// 通过 TryCreate 构建；不要手动修改生成的数据。
struct CoordinateConversion {
    Mat4 Matrix;
    Mat4 InverseMatrix;
    Mat3 Axes; // 不含单位缩放的方向换基矩阵。
    float UnitScale = 1.0f;
    bool ReversesWinding = false;

    static bool TryCreate(const CoordinateSystem& source, const CoordinateSystem& target, CoordinateConversion& output) {
        Vector3 sourceBasis[3], targetBasis[3];
        if (!CoordinateDetail::GetBasis(source, sourceBasis) || !CoordinateDetail::GetBasis(target, targetBasis)) return false;
        const double ratio = static_cast<double>(source.MetersPerUnit) / target.MetersPerUnit;
        const float scale = static_cast<float>(ratio);
        const float inverseScale = static_cast<float>(1.0 / ratio);
        if (!std::isfinite(scale) || !std::isfinite(inverseScale) || scale <= 0.0f || inverseScale <= 0.0f) return false;
        CoordinateConversion result;
        result.UnitScale = scale;
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 3; ++column) {
                float axis = 0.0f;
                for (std::size_t k = 0; k < 3; ++k) {
                    axis += CoordinateDetail::Element(targetBasis[k], row) * CoordinateDetail::Element(sourceBasis[k], column);
                }
                result.Axes(row, column) = axis;
                result.Matrix(row, column) = axis * scale;
                result.InverseMatrix(column, row) = axis * inverseScale;
            }
        }
        result.ReversesWinding = source.Hand != target.Hand;
        output = result;
        return true;
    }

    Vector3 ConvertPosition(Vector3 value) const {
        const Vector4 result = Matrix * Vector4(value.X, value.Y, value.Z, 1.0f);
        return {result.X, result.Y, result.Z};
    }
    Vector3 ConvertDirection(Vector3 value) const { return Axes * value; }
    // 本类仅支持正的统一单位缩放和正交轴换基，因此法线方向可按 Axes 变换。
    // 任意模型的非均匀缩放，仍须使用该模型线性变换的逆转置处理法线。
    Vector3 ConvertNormal(Vector3 value) const { return Axes * value; }
    float ConvertDistance(float value) const { return value * UnitScale; }
    Ray ConvertRay(const Ray& value) const { return {ConvertPosition(value.Origin), ConvertDirection(value.Direction)}; }
    Plane ConvertPlane(const Plane& value) const { return {ConvertNormal(value.Normal), ConvertDistance(value.Distance)}; }
    Bounds ConvertBounds(const Bounds& value) const {
        const Vector3 extent = value.Extents;
        return {ConvertPosition(value.Center),
                {std::fabs(Matrix(0,0))*extent.X + std::fabs(Matrix(0,1))*extent.Y + std::fabs(Matrix(0,2))*extent.Z,
                 std::fabs(Matrix(1,0))*extent.X + std::fabs(Matrix(1,1))*extent.Y + std::fabs(Matrix(1,2))*extent.Z,
                 std::fabs(Matrix(2,0))*extent.X + std::fabs(Matrix(2,1))*extent.Y + std::fabs(Matrix(2,2))*extent.Z}};
    }
    // 局部变换的输入、输出空间都切换约定：M' = C * M * inverse(C)。
    // 模型节点与骨骼局部变换可走此入口；不是简单在矩阵左侧乘一次 C。
    Mat4 ConvertTransform(const Mat4& value) const { return Matrix * value * InverseMatrix; }
    bool TryConvertRotation(const Quaternion& value, Quaternion& output) const {
        Quaternion unit;
        if (!value.TryNormalize(unit)) return false;
        // 正交换基 A 下：四元数的向量部分变成 det(A) * A * xyz，W 不变。
        // 这同时处理左右手转换，不把单位缩放混入旋转。
        Vector3 vector = Axes * Vector3(unit.X, unit.Y, unit.Z);
        if (ReversesWinding) vector = -vector;
        output = {vector.X, vector.Y, vector.Z, unit.W};
        return true;
    }
};
}
