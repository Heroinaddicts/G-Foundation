#pragma once
#include "SceneGizmo.h"
#include "Math/Projection.h"
#include "Math/TransformMath.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace SceneGizmoDetail {
using GameSproutRuntime::Vector2;
using GameSproutRuntime::Vector3;
using GameSproutRuntime::Vector4;
using GameSproutRuntime::Quaternion;
using GameSproutRuntime::Mat4;
using GameSproutRuntime::Ray;
inline bool Finite(Vector2 value) {return std::isfinite(value.X)&&std::isfinite(value.Y);}
inline bool Finite(Vector3 value) {return GameSproutRuntime::RuntimeMath::IsFinite(value);}
inline Vector3 Point(const Mat4& matrix,Vector3 value) {
    const Vector4 result=matrix*Vector4{value.X,value.Y,value.Z,1};return {result.X,result.Y,result.Z};
}
inline Vector3 Direction(const Mat4& matrix,Vector3 value) {
    const Vector4 result=matrix*Vector4{value.X,value.Y,value.Z,0};return {result.X,result.Y,result.Z};
}
inline Quaternion Multiply(Quaternion a,Quaternion b) {
    return {a.W*b.X+a.X*b.W+a.Y*b.Z-a.Z*b.Y,a.W*b.Y-a.X*b.Z+a.Y*b.W+a.Z*b.X,
        a.W*b.Z+a.X*b.Y-a.Y*b.X+a.Z*b.W,a.W*b.W-a.X*b.X-a.Y*b.Y-a.Z*b.Z};
}
inline Quaternion AngleAxis(Vector3 axis,double angle) {
    const double half=angle*.5,sine=std::sin(half);
    return {float(axis.X*sine),float(axis.Y*sine),float(axis.Z*sine),float(std::cos(half))};
}
inline Mat4 Compose(const SceneGizmoTransform& value) {
    return GameSproutRuntime::RuntimeMath::ComposeTRS(value.Position,value.Rotation,value.Scale);
}
inline Mat4 AroundPivot(Mat4 linear,Vector3 pivot) {
    const Vector3 offset=pivot-Direction(linear,pivot);
    linear(0,3)=offset.X;linear(1,3)=offset.Y;linear(2,3)=offset.Z;return linear;
}
inline Mat4 ScaleAlongAxes(const Vector3 (&axes)[3],Vector3 factors,Vector3 pivot) {
    Mat4 result;
    const float multipliers[]={factors.X-1,factors.Y-1,factors.Z-1};
    for(int axis=0;axis<3;++axis) {
        const float values[]={axes[axis].X,axes[axis].Y,axes[axis].Z};
        for(int row=0;row<3;++row)for(int column=0;column<3;++column)
            result(row,column)+=multipliers[axis]*values[row]*values[column];
    }
    return AroundPivot(result,pivot);
}
inline bool RayPlane(const Ray& ray,Vector3 origin,Vector3 normal,Vector3& output) {
    const double denominator=normal.Dot(ray.Direction);
    if(!std::isfinite(denominator)||std::fabs(denominator)<1e-7) return false;
    const double distance=normal.Dot(origin-ray.Origin)/denominator;
    if(!std::isfinite(distance)||distance<0||distance>std::numeric_limits<float>::max()) return false;
    const Vector3 candidate=ray.GetPoint(float(distance));if(!Finite(candidate))return false;
    output=candidate;return true;
}
inline float Snap(float value,float step,bool enabled) {
    if(!enabled||step<=0) return value;
    return float(std::round(double(value)/step)*step);
}
inline int AxisIndex(SceneGizmoHandle handle) {
    return handle==SceneGizmoHandle::X?0:handle==SceneGizmoHandle::Y?1:handle==SceneGizmoHandle::Z?2:-1;
}
inline bool PlaneAxes(SceneGizmoHandle handle,int& first,int& second) {
    if(handle==SceneGizmoHandle::XY) {first=0;second=1;return true;}
    if(handle==SceneGizmoHandle::YZ) {first=1;second=2;return true;}
    if(handle==SceneGizmoHandle::ZX) {first=2;second=0;return true;}
    return false;
}
inline float Component(Vector3 value,int index) {return index==0?value.X:index==1?value.Y:value.Z;}
inline void SetComponent(Vector3& value,int index,float component) {
    if(index==0)value.X=component;else if(index==1)value.Y=component;else value.Z=component;
}
// T*R*S 有四组等价的双轴翻转分支。矩阵分解只给出其中一组；编辑时优先保持
// 起始缩放的各轴符号，避免正常旋转时突然出现两个负号及额外 180 度旋转。
inline void PreserveScaleSigns(SceneGizmoTransform& candidate,const SceneGizmoTransform& initial) {
    Quaternion reference;if(!initial.Rotation.TryNormalize(reference))return;
    const Quaternion flips[]={{0,0,0,1},{1,0,0,0},{0,1,0,0},{0,0,1,0}};
    const Vector3 signs[]={{1,1,1},{1,-1,-1},{-1,1,-1},{-1,-1,1}};
    SceneGizmoTransform best=candidate;int bestMatches=-1;float bestAlignment=-1;
    for(int branch=0;branch<4;++branch) {
        SceneGizmoTransform alternative=candidate;
        alternative.Scale={candidate.Scale.X*signs[branch].X,candidate.Scale.Y*signs[branch].Y,candidate.Scale.Z*signs[branch].Z};
        if(!Multiply(candidate.Rotation,flips[branch]).TryNormalize(alternative.Rotation))continue;
        int matches=0;
        for(int axis=0;axis<3;++axis) {
            const float before=Component(initial.Scale,axis),after=Component(alternative.Scale,axis);
            if(before!=0&&after!=0&&std::signbit(before)==std::signbit(after))++matches;
        }
        const float dot=reference.X*alternative.Rotation.X+reference.Y*alternative.Rotation.Y+
            reference.Z*alternative.Rotation.Z+reference.W*alternative.Rotation.W;
        const float alignment=std::fabs(dot);
        if(matches>bestMatches||(matches==bestMatches&&alignment>bestAlignment)) {
            // q 与 -q 是同一旋转，保持与起始四元数同半球。
            if(dot<0)alternative.Rotation={-alternative.Rotation.X,-alternative.Rotation.Y,-alternative.Rotation.Z,-alternative.Rotation.W};
            best=alternative;bestMatches=matches;bestAlignment=alignment;
        }
    }
    candidate=best;
}
inline float DistanceSquared(Vector2 a,Vector2 b) {
    const double x=double(a.X)-b.X,y=double(a.Y)-b.Y;return float(x*x+y*y);
}
inline float SegmentDistance(Vector2 point,Vector2 a,Vector2 b) {
    const Vector2 difference=b-a;const double length=double(difference.X)*difference.X+double(difference.Y)*difference.Y;
    if(length<=1e-12)return std::sqrt(DistanceSquared(point,a));
    const Vector2 offset=point-a;
    const float t=float(std::clamp((double(offset.X)*difference.X+double(offset.Y)*difference.Y)/length,0.0,1.0));
    return std::sqrt(DistanceSquared(point,a+difference*t));
}
inline bool Projection(const SceneGizmoView& view,Mat4& output) {
    if(!view.Width||!view.Height)return false;
    const auto& settings=view.Camera.Projection;const float aspect=float(view.Width)/view.Height;
    if(settings.Projection==GameSproutRuntime::CameraProjection::Perspective)
        return GameSproutRuntime::TryMakePerspective(settings.VerticalFovRadians,aspect,settings.NearPlane,settings.FarPlane,output);
    if(settings.Projection==GameSproutRuntime::CameraProjection::Orthographic)
        return GameSproutRuntime::TryMakeOrthographic(settings.OrthographicHeight,aspect,settings.NearPlane,settings.FarPlane,output);
    return false;
}
inline void PlaneQuad(Vector3 pivot,const Vector3 (&axes)[3],int first,int second,float length,Vector3 (&corners)[4]) {
    constexpr float low=.22f,high=.43f;
    corners[0]=pivot+(axes[first]*low+axes[second]*low)*length;
    corners[1]=pivot+(axes[first]*high+axes[second]*low)*length;
    corners[2]=pivot+(axes[first]*high+axes[second]*high)*length;
    corners[3]=pivot+(axes[first]*low+axes[second]*high)*length;
}
}
