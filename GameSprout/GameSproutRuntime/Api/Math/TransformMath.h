#pragma once

#include "Mat4.h"
#include "Quaternion.h"
#include "Vector3.h"
#include "Color.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace GameSproutRuntime::RuntimeMath {
inline bool IsFinite(Vector3 value) {
    return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
}
inline bool IsFinite(Color value) {
    return std::isfinite(value.R) && std::isfinite(value.G) && std::isfinite(value.B) && std::isfinite(value.A);
}
inline bool IsFinite(const Mat4& value) {
    for (float element : value.Data) if (!std::isfinite(element)) return false;
    return true;
}

// 输入位置/缩放必须有限，旋转必须可归一化。无效输入抛 invalid_argument；
// 正常 Transform 写入在 setter 中先校验，读取矩阵时不会走到此失败分支。
inline Mat4 ComposeTRS(Vector3 position, Quaternion rotation, Vector3 scale) {
    Quaternion q;
    if (!IsFinite(position) || !IsFinite(scale) || !rotation.TryNormalize(q))
        throw std::invalid_argument("Invalid TRS input");
    // 中间量使用 double，避免在接近单位四元数边界时放大浮点舍入。
    const double inverseLength=1.0/std::sqrt(static_cast<double>(q.X)*q.X+static_cast<double>(q.Y)*q.Y+
        static_cast<double>(q.Z)*q.Z+static_cast<double>(q.W)*q.W);
    const double x=q.X*inverseLength, y=q.Y*inverseLength, z=q.Z*inverseLength, w=q.W*inverseLength;
    Mat4 result;
    result(0,0)=static_cast<float>((1.0-2.0*(y*y+z*z))*scale.X);
    result(1,0)=static_cast<float>((2.0*(x*y+z*w))*scale.X);
    result(2,0)=static_cast<float>((2.0*(x*z-y*w))*scale.X);
    result(0,1)=static_cast<float>((2.0*(x*y-z*w))*scale.Y);
    result(1,1)=static_cast<float>((1.0-2.0*(x*x+z*z))*scale.Y);
    result(2,1)=static_cast<float>((2.0*(y*z+x*w))*scale.Y);
    result(0,2)=static_cast<float>((2.0*(x*z+y*w))*scale.Z);
    result(1,2)=static_cast<float>((2.0*(y*z-x*w))*scale.Z);
    result(2,2)=static_cast<float>((1.0-2.0*(x*x+y*y))*scale.Z);
    result(0,3)=position.X; result(1,3)=position.Y; result(2,3)=position.Z;
    if (!IsFinite(result)) throw std::invalid_argument("TRS matrix is not representable");
    return result;
}

namespace Detail {
struct DoubleVector3 { double X=0, Y=0, Z=0; };
inline double Dot(DoubleVector3 a, DoubleVector3 b) { return a.X*b.X+a.Y*b.Y+a.Z*b.Z; }
inline DoubleVector3 Cross(DoubleVector3 a, DoubleVector3 b) {
    return {a.Y*b.Z-a.Z*b.Y, a.Z*b.X-a.X*b.Z, a.X*b.Y-a.Y*b.X};
}
inline DoubleVector3 Scale(DoubleVector3 value, double scale) { return {value.X*scale,value.Y*scale,value.Z*scale}; }
inline double Length(DoubleVector3 value) { return std::hypot(value.X,value.Y,value.Z); }
inline DoubleVector3 Normalize(DoubleVector3 value) { return Scale(value,1.0/Length(value)); }
inline double Element(DoubleVector3 value, std::size_t row) { return row==0?value.X:row==1?value.Y:value.Z; }
inline bool IsAffine(const Mat4& value) {
    // 世界/局部 TRS 运算产生的最后一行应精确是 (0,0,0,1)。
    // 不接受投影矩阵或偷偷丢弃最后一行的非仿射信息。
    return IsFinite(value) && value(3,0)==0.0f && value(3,1)==0.0f && value(3,2)==0.0f && value(3,3)==1.0f;
}
inline DoubleVector3 Column(const Mat4& matrix,std::size_t column) {
    return {matrix(0,column),matrix(1,column),matrix(2,column)};
}
inline bool ToFloat(double value,float& output) {
    if (!std::isfinite(value) || std::fabs(value)>std::numeric_limits<float>::max()) return false;
    output=static_cast<float>(value);
    return std::isfinite(output);
}
inline Quaternion RotationQuaternion(const DoubleVector3 (&columns)[3]) {
    const double m00=columns[0].X, m01=columns[1].X, m02=columns[2].X;
    const double m10=columns[0].Y, m11=columns[1].Y, m12=columns[2].Y;
    const double m20=columns[0].Z, m21=columns[1].Z, m22=columns[2].Z;
    const double trace=m00+m11+m22;
    Quaternion result;
    if (trace>0) {
        const double s=std::sqrt(trace+1.0)*2.0;
        result={static_cast<float>((m21-m12)/s),static_cast<float>((m02-m20)/s),static_cast<float>((m10-m01)/s),static_cast<float>(0.25*s)};
    } else if (m00>m11 && m00>m22) {
        const double s=std::sqrt(1.0+m00-m11-m22)*2.0;
        result={static_cast<float>(0.25*s),static_cast<float>((m01+m10)/s),static_cast<float>((m02+m20)/s),static_cast<float>((m21-m12)/s)};
    } else if (m11>m22) {
        const double s=std::sqrt(1.0+m11-m00-m22)*2.0;
        result={static_cast<float>((m01+m10)/s),static_cast<float>(0.25*s),static_cast<float>((m12+m21)/s),static_cast<float>((m02-m20)/s)};
    } else {
        const double s=std::sqrt(1.0+m22-m00-m11)*2.0;
        result={static_cast<float>((m02+m20)/s),static_cast<float>((m12+m21)/s),static_cast<float>(0.25*s),static_cast<float>((m10-m01)/s)};
    }
    return result;
}
}

// 支持非均匀缩放、负缩放和一般可逆仿射变换。奇异/近共线的线性基拒绝；
// double 中间计算后仍不能用 float 表示逆矩阵时失败，不修改 output。
inline bool TryInverseAffine(const Mat4& value, Mat4& output) {
    if (!Detail::IsAffine(value)) return false;
    Detail::DoubleVector3 columns[3];
    double lengths[3];
    for (std::size_t i=0;i<3;++i) {
        columns[i]=Detail::Column(value,i);
        lengths[i]=Detail::Length(columns[i]);
        if (lengths[i]==0.0) return false;
        columns[i]=Detail::Scale(columns[i],1.0/lengths[i]);
    }
    const double determinant=Detail::Dot(columns[0],Detail::Cross(columns[1],columns[2]));
    if (std::fabs(determinant)<=1e-10) return false;
    const Detail::DoubleVector3 rows[3]={
        Detail::Cross(columns[1],columns[2]),
        Detail::Cross(columns[2],columns[0]),
        Detail::Cross(columns[0],columns[1])};
    const Detail::DoubleVector3 translation=Detail::Column(value,3);
    Mat4 result;
    for (std::size_t row=0;row<3;++row) {
        const Detail::DoubleVector3 inverseRow=Detail::Scale(rows[row],1.0/(determinant*lengths[row]));
        for (std::size_t column=0;column<3;++column)
            if (!Detail::ToFloat(Detail::Element(inverseRow,column),result(row,column))) return false;
        if (!Detail::ToFloat(-Detail::Dot(inverseRow,translation),result(row,3))) return false;
    }
    output=result;
    return true;
}

// 分解为 T*R*S，拒绝剪切。反射的负号归入最大尺寸轴，分解结果不保证与原始
// 四元数/各轴符号逐项相同，但重新组合的矩阵相同。零缩放轴使用确定性的补全基。
// 失败时 position/rotation/scale 全部保持原值。
inline bool TryDecomposeTRS(const Mat4& value, Vector3& position, Quaternion& rotation, Vector3& scale) {
    if (!Detail::IsAffine(value)) return false;
    constexpr double orthogonalTolerance=1e-5;
    Detail::DoubleVector3 columns[3];
    double lengths[3];
    std::size_t count=0;
    for (std::size_t i=0;i<3;++i) {
        columns[i]=Detail::Column(value,i);
        lengths[i]=Detail::Length(columns[i]);
        if (lengths[i]>std::numeric_limits<float>::max()) return false;
        if (lengths[i]>0.0) { columns[i]=Detail::Scale(columns[i],1.0/lengths[i]); ++count; }
    }
    for (std::size_t a=0;a<3;++a) for (std::size_t b=a+1;b<3;++b)
        if (lengths[a]>0 && lengths[b]>0 && std::fabs(Detail::Dot(columns[a],columns[b]))>orthogonalTolerance) return false;
    if (count==3) {
        const double determinant=Detail::Dot(columns[0],Detail::Cross(columns[1],columns[2]));
        if (std::fabs(std::fabs(determinant)-1.0)>orthogonalTolerance*3.0) return false;
        if (determinant<0) {
            std::size_t largest=0;
            if (lengths[1]>lengths[largest]) largest=1;
            if (lengths[2]>lengths[largest]) largest=2;
            columns[largest]=Detail::Scale(columns[largest],-1);
            lengths[largest]=-lengths[largest];
        }
    } else if (count==2) {
        if (lengths[0]==0) columns[0]=Detail::Normalize(Detail::Cross(columns[1],columns[2]));
        else if (lengths[1]==0) columns[1]=Detail::Normalize(Detail::Cross(columns[2],columns[0]));
        else columns[2]=Detail::Normalize(Detail::Cross(columns[0],columns[1]));
    } else if (count==1) {
        const std::size_t known=lengths[0]>0?0:lengths[1]>0?1:2;
        const auto axis=columns[known];
        // 选择最不平行的单位轴，避免构造补全方向时精度退化。
        Detail::DoubleVector3 helper;
        if (std::fabs(axis.X)<=std::fabs(axis.Y) && std::fabs(axis.X)<=std::fabs(axis.Z)) helper={1,0,0};
        else if (std::fabs(axis.Y)<=std::fabs(axis.Z)) helper={0,1,0};
        else helper={0,0,1};
        const std::size_t next=(known+1)%3, last=(known+2)%3;
        columns[next]=Detail::Normalize(Detail::Cross(helper,axis));
        columns[last]=Detail::Cross(axis,columns[next]);
    } else {
        columns[0]={1,0,0}; columns[1]={0,1,0}; columns[2]={0,0,1};
    }
    Quaternion candidateRotation;
    if (!Detail::RotationQuaternion(columns).TryNormalize(candidateRotation)) return false;
    const Vector3 candidatePosition{value(0,3),value(1,3),value(2,3)};
    const Vector3 candidateScale{static_cast<float>(lengths[0]),static_cast<float>(lengths[1]),static_cast<float>(lengths[2])};
    Mat4 recomposed;
    try { recomposed=ComposeTRS(candidatePosition,candidateRotation,candidateScale); }
    catch (const std::invalid_argument&) { return false; }
    for (std::size_t column=0;column<3;++column) {
        const double tolerance=std::max(std::fabs(lengths[column])*2e-5,
            static_cast<double>(std::numeric_limits<float>::denorm_min())*8.0);
        for (std::size_t row=0;row<3;++row)
            if (std::fabs(static_cast<double>(recomposed(row,column))-value(row,column))>tolerance) return false;
    }
    position=candidatePosition; rotation=candidateRotation; scale=candidateScale;
    return true;
}
}
