#pragma once
#include "Api/RuntimeRenderApi.h"
#include "Api/Math/TransformMath.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace GameSproutRuntime::RuntimeShadowMath {

struct Cascade {
    // Canonical Runtime clip coordinates: X/Y [-1,1], Z [0,1]. The light
    // camera faces -TowardsLight; zero depth is nearest the light source.
    // A top-first texture is sampled at (clip.x*.5+.5, .5-clip.y*.5).
    Mat4 ViewProjection;
    float NearDepth=0,FarDepth=0; // Positive depth in the rendering camera's view.
    Vector3 Center;
    float Radius=0;
    float WorldUnitsPerTexel=0;
    float DepthRange=0;
};

struct CascadeSet {
    std::uint32_t Count=0;
    std::array<Cascade,4> Entries{};
};

namespace Detail {
struct Point {double X=0,Y=0,Z=0;};
inline Point operator+(Point a,Point b){return {a.X+b.X,a.Y+b.Y,a.Z+b.Z};}
inline Point operator-(Point a,Point b){return {a.X-b.X,a.Y-b.Y,a.Z-b.Z};}
inline Point operator*(Point a,double b){return {a.X*b,a.Y*b,a.Z*b};}
inline double Dot(Vector3 axis,Point point){return axis.X*point.X+axis.Y*point.Y+axis.Z*point.Z;}
inline bool Finite(Point p){return std::isfinite(p.X)&&std::isfinite(p.Y)&&std::isfinite(p.Z);}
inline bool Float(double value,float& result){
    if(!std::isfinite(value)||std::fabs(value)>std::numeric_limits<float>::max())return false;
    result=static_cast<float>(value);return true;
}
inline Point World(const Mat4& inverseView,double x,double y,double z){
    return {inverseView(0,0)*x+inverseView(0,1)*y+inverseView(0,2)*z+inverseView(0,3),
        inverseView(1,0)*x+inverseView(1,1)*y+inverseView(1,2)*z+inverseView(1,3),
        inverseView(2,0)*x+inverseView(2,1)*y+inverseView(2,2)*z+inverseView(2,3)};
}
inline double Extent(Vector3 axis,Vector3 extents){
    return std::fabs(double(axis.X))*extents.X+std::fabs(double(axis.Y))*extents.Y+std::fabs(double(axis.Z))*extents.Z;
}
inline double RowLength(const Mat4& matrix,std::size_t row){
    return std::hypot(double(matrix(row,0)),double(matrix(row,1)),double(matrix(row,2)));
}
inline Point SliceCorner(const Mat4& inverseView,float aspect,bool perspective,double tangent,double orthoHeight,
    double nearDepth,double farDepth,std::size_t index){
    const double depth=index<4?nearDepth:farDepth;
    const double halfHeight=perspective?depth*tangent:orthoHeight*.5;
    return World(inverseView,(index&1?1.0:-1.0)*halfHeight*aspect,(index&2?1.0:-1.0)*halfHeight,-depth);
}
}

// Retaining a map is safe while each of the current camera's receiver slices
// fits inside its old light tile and depth range, including the shader's offset.
inline std::uint32_t CoveredCascades(const RenderCamera& camera,float aspect,const Mat4* projection,const float* splits,
    std::uint32_t count,std::uint32_t tile,float normalBias,bool soft) {
    Mat4 inverseView;
    if(!RuntimeMath::TryInverseAffine(camera.View,inverseView))return 0;
    const bool perspective=camera.Projection.Projection==CameraProjection::Perspective;
    const double tangent=perspective?std::tan(double(camera.Projection.VerticalFovRadians)*.5):0;
    const double xyLimit=1-2*(double(normalBias)+(soft?1:0))/tile;
    double nearDepth=camera.Projection.NearPlane;
    std::uint32_t covered=0;
    for(std::uint32_t cascade=0;cascade<count;++cascade) {
        const auto& matrix=projection[cascade];
        const double worldTexel=2/(Detail::RowLength(matrix,0)*tile);
        const double zMargin=normalBias*worldTexel*Detail::RowLength(matrix,2);
        const double farDepth=splits[cascade];
        bool inside=true;
        for(std::size_t index=0;index<8;++index) {
            const auto p=Detail::SliceCorner(inverseView,aspect,perspective,tangent,
                camera.Projection.OrthographicHeight,nearDepth,farDepth,index);
            const auto clip=[&](std::size_t row){
                return matrix(row,0)*p.X+matrix(row,1)*p.Y+matrix(row,2)*p.Z+matrix(row,3);
            };
            const double z=clip(2);
            if(!(std::fabs(clip(0))<=xyLimit&&std::fabs(clip(1))<=xyLimit&&z>=zMargin&&z<=1-zMargin)) {
                inside=false;
                break;
            }
        }
        if(inside)covered|=std::uint32_t{1}<<cascade;
        nearDepth=farDepth;
    }
    return covered;
}

// Fits one square, stable light projection per camera depth slice. Perspective
// splits blend logarithmic and uniform distances equally; orthographic splits
// are uniform. Camera projection is limited by shadowDistance, never extended.
//
// casterWorldBounds must include candidate casters outside the camera frustum:
// a directional shadow may enter the view from an invisible caster. Bounds
// whose light-space XY projection intersects a slice extend its depth fit.
// Their XY position cannot affect the stable receiver fit. Supply all relevant
// active casting layers, then cull actual draws against each resulting matrix.
// A masked slice still reports its near/far split; its projection is left unset.
// Empty caster input is valid. Invalid input leaves output unchanged.
inline bool Build(const RenderCamera& camera,float aspect,Vector3 towardsLight,float shadowDistance,
    std::uint32_t cascadeCount,std::uint32_t tileResolution,const Bounds* casterWorldBounds,
    std::size_t casterCount,CascadeSet& output,double receiverFitScale=1,const Mat4* retainedXY=nullptr,
    std::uint32_t fitMask=0xF) {
    if((cascadeCount!=1&&cascadeCount!=2&&cascadeCount!=4)||tileResolution<8||tileResolution>32768||
       !std::isfinite(aspect)||aspect<=0||!std::isfinite(shadowDistance)||
       !std::isfinite(receiverFitScale)||receiverFitScale<1||
       (casterCount&&!casterWorldBounds))return false;
    const auto& projection=camera.Projection;
    const bool perspective=projection.Projection==CameraProjection::Perspective;
    if(!perspective&&projection.Projection!=CameraProjection::Orthographic)return false;
    if(!std::isfinite(projection.NearPlane)||!std::isfinite(projection.FarPlane)||
       projection.NearPlane<0||(perspective&&projection.NearPlane==0)||
       projection.FarPlane<=projection.NearPlane)return false;
    const double nearDepth=projection.NearPlane;
    const double farDepth=std::min(double(projection.FarPlane),double(shadowDistance));
    if(farDepth<=nearDepth)return false;
    double tangent=0,orthoHeight=projection.OrthographicHeight;
    if(perspective) {
        if(!std::isfinite(projection.VerticalFovRadians)||projection.VerticalFovRadians<=0||
           projection.VerticalFovRadians>=Pi)return false;
        tangent=std::tan(double(projection.VerticalFovRadians)*.5);
    }else if(!std::isfinite(orthoHeight)||orthoHeight<=0)return false;
    Mat4 inverseView;
    if(!RuntimeMath::TryInverseAffine(camera.View,inverseView))return false;
    Vector3 light,right,up;
    if(!towardsLight.TryNormalize(light))return false;
    const Vector3 referenceUp=std::fabs(light.Y)>.99f?Vector3{0,0,1}:Vector3{0,1,0};
    if(!referenceUp.Cross(-light).TryNormalize(right))return false;
    up=(-light).Cross(right);
    for(std::size_t index=0;index<casterCount;++index) {
        const auto& bound=casterWorldBounds[index];
        if(!RuntimeMath::IsFinite(bound.Center)||!RuntimeMath::IsFinite(bound.Extents)||
           bound.Extents.X<0||bound.Extents.Y<0||bound.Extents.Z<0)return false;
    }
    CascadeSet result;result.Count=cascadeCount;
    double sliceNear=nearDepth;
    for(std::uint32_t cascade=0;cascade<cascadeCount;++cascade) {
        const double fraction=double(cascade+1)/cascadeCount;
        const double uniform=nearDepth+(farDepth-nearDepth)*fraction;
        const double sliceFar=cascade+1==cascadeCount?farDepth:
            (perspective?.5*(uniform+nearDepth*std::pow(farDepth/nearDepth,fraction)):uniform);
        auto& entry=result.Entries[cascade];
        if(!Detail::Float(sliceNear,entry.NearDepth)||!Detail::Float(sliceFar,entry.FarDepth)||entry.FarDepth<=entry.NearDepth)return false;
        if(!(fitMask&(std::uint32_t{1}<<cascade))) {
            sliceNear=sliceFar;
            continue;
        }
        std::array<Detail::Point,8> corners;
        Detail::Point center;
        for(std::size_t index=0;index<corners.size();++index) {
            corners[index]=Detail::SliceCorner(inverseView,aspect,perspective,tangent,orthoHeight,sliceNear,sliceFar,index);
            if(!Detail::Finite(corners[index]))return false;
            center=center+corners[index]*(1.0/8.0);
        }
        double radiusSquared=0;
        for(const auto corner:corners){const auto d=corner-center;radiusSquared=std::max(radiusSquared,d.X*d.X+d.Y*d.Y+d.Z*d.Z);}
        // Quantization removes rotation/translation rounding noise from the fit.
        // Two texels of padding retain the receiver after center snapping and
        // leave room for a one-texel 3x3 PCF footprint inside the cascade tile.
        const double receiverRadius=std::max(1.0/16.0,std::ceil(std::sqrt(radiusSquared)*receiverFitScale*16.0)/16.0);
        const auto* previous=retainedXY?&retainedXY[cascade]:nullptr;
        const double radius=previous?1/Detail::RowLength(*previous,0):receiverRadius*tileResolution/(tileResolution-4.0);
        const double texel=radius*2/tileResolution;
        if(!std::isfinite(radius)||!std::isfinite(texel)||texel<=0)return false;
        const double centerX=previous?-(*previous)(0,3)*radius:std::round(Detail::Dot(right,center)/texel)*texel;
        const double centerY=previous?-(*previous)(1,3)*radius:std::round(Detail::Dot(up,center)/texel)*texel;
        double minimumZ=std::numeric_limits<double>::infinity(),maximumZ=-minimumZ;
        for(const auto corner:corners){const double z=Detail::Dot(light,corner);minimumZ=std::min(minimumZ,z);maximumZ=std::max(maximumZ,z);}
        for(std::size_t index=0;index<casterCount;++index) {
            const auto& bound=casterWorldBounds[index];
            const Detail::Point point{bound.Center.X,bound.Center.Y,bound.Center.Z};
            if(std::fabs(Detail::Dot(right,point)-centerX)>radius+Detail::Extent(right,bound.Extents)||
               std::fabs(Detail::Dot(up,point)-centerY)>radius+Detail::Extent(up,bound.Extents))continue;
            const double z=Detail::Dot(light,point),extent=Detail::Extent(light,bound.Extents);
            minimumZ=std::min(minimumZ,z-extent);maximumZ=std::max(maximumZ,z+extent);
        }
        // A moving receiver can use the XY guard band while advancing toward
        // or away from the sun. Keep corresponding slack in the cached depth.
        const double padding=std::max(1.0,texel*2)+(receiverFitScale-1)*radius;
        const double depthRange=maximumZ-minimumZ+padding*2;
        const double lightEyeZ=maximumZ+padding;
        const auto snappedCenter=Detail::Point{right.X,right.Y,right.Z}*centerX+
            Detail::Point{up.X,up.Y,up.Z}*centerY+Detail::Point{light.X,light.Y,light.Z}*Detail::Dot(light,center);
        if(!Detail::Float(radius,entry.Radius)||!Detail::Float(texel,entry.WorldUnitsPerTexel)||
           !Detail::Float(depthRange,entry.DepthRange)||entry.DepthRange<=0||
           !Detail::Float(snappedCenter.X,entry.Center.X)||!Detail::Float(snappedCenter.Y,entry.Center.Y)||
           !Detail::Float(snappedCenter.Z,entry.Center.Z))return false;
        // Compose orthographic * view directly in double. This avoids a large
        // translated light-eye intermediate before fitting world coordinates.
        Mat4 matrix;
        const double rows[3][4]={{right.X/radius,right.Y/radius,right.Z/radius,-centerX/radius},
            {up.X/radius,up.Y/radius,up.Z/radius,-centerY/radius},
            {-light.X/depthRange,-light.Y/depthRange,-light.Z/depthRange,lightEyeZ/depthRange}};
        for(std::size_t row=0;row<3;++row)for(std::size_t column=0;column<4;++column)
            if(!Detail::Float(rows[row][column],matrix(row,column)))return false;
        if(previous)for(std::size_t row=0;row<2;++row)for(std::size_t column=0;column<4;++column)
            matrix(row,column)=(*previous)(row,column);
        if(!RuntimeMath::IsFinite(matrix))return false;
        entry.ViewProjection=matrix;
        sliceNear=sliceFar;
    }
    output=result;return true;
}
}
