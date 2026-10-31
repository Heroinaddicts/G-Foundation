#pragma once
#include "RuntimeMath.h"
#include <limits>

namespace GameSproutRuntime::PickingMath {
// 射线变换与三角形相交用double，避免大/小比例父变换造成float中间溢出。
struct Vector {
    double X=0,Y=0,Z=0;
    Vector()=default;
    Vector(double x,double y,double z):X(x),Y(y),Z(z) {}
    explicit Vector(Vector3 value):X(value.X),Y(value.Y),Z(value.Z) {}
    Vector operator+(Vector other) const {return {X+other.X,Y+other.Y,Z+other.Z};}
    Vector operator-(Vector other) const {return {X-other.X,Y-other.Y,Z-other.Z};}
    Vector operator*(double scale) const {return {X*scale,Y*scale,Z*scale};}
    double Dot(Vector other) const {return X*other.X+Y*other.Y+Z*other.Z;}
    Vector Cross(Vector other) const {return {Y*other.Z-Z*other.Y,Z*other.X-X*other.Z,X*other.Y-Y*other.X};}
};
inline Vector Transform(const Mat4& matrix,Vector value,double w) {
    return {matrix(0,0)*value.X+matrix(0,1)*value.Y+matrix(0,2)*value.Z+matrix(0,3)*w,
        matrix(1,0)*value.X+matrix(1,1)*value.Y+matrix(1,2)*value.Z+matrix(1,3)*w,
        matrix(2,0)*value.X+matrix(2,1)*value.Y+matrix(2,2)*value.Z+matrix(2,3)*w};
}
inline bool ToVector3(Vector value,Vector3& result) {
    const double maximum=std::numeric_limits<float>::max();
    if(!std::isfinite(value.X)||!std::isfinite(value.Y)||!std::isfinite(value.Z)||
       std::fabs(value.X)>maximum||std::fabs(value.Y)>maximum||std::fabs(value.Z)>maximum) return false;
    result={static_cast<float>(value.X),static_cast<float>(value.Y),static_cast<float>(value.Z)};return true;
}
inline bool Intersect(Vector origin,Vector direction,Vector a,Vector b,Vector c,double& distance) {
    const Vector first=b-a,second=c-a,p=direction.Cross(second);
    const double determinant=first.Dot(p);
    const double scale=std::sqrt(first.Dot(first)*second.Dot(second)*direction.Dot(direction));
    if(!std::isfinite(determinant)||!std::isfinite(scale)||scale==0||std::fabs(determinant)<=1e-12*scale) return false;
    const double inverse=1.0/determinant;
    const Vector from=origin-a;
    const double u=from.Dot(p)*inverse;
    if(u < -1e-10 || u > 1.0+1e-10) return false;
    const Vector q=from.Cross(first);
    const double v=direction.Dot(q)*inverse;
    if(v < -1e-10 || u+v > 1.0+1e-10) return false;
    const double t=second.Dot(q)*inverse;
    if(!std::isfinite(t)||t<0||t>std::numeric_limits<float>::max()) return false;
    distance=t;return true;
}
}
