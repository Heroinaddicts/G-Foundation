#pragma once
#include "Api/Math/Mat4.h"
#include "Api/Math/Vector3.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace GameSproutRuntime::RenderFrustum {
// Canonical Runtime clip space: -w <= x,y <= w and 0 <= z <= w.
// A box is rejected only when its complete affine image is outside one plane.
// All calculations use double and a float-scale guard band; uncertain/invalid
// inputs are retained. No viewport or scene state is stored between frames.
struct Box {
    float Model[16]{};
    double Center[3]{},Extent[3]{},Weight[4]{1,1,1,1};
    bool Valid=false;
    Box()=default;
    Box(Vector3 minimum,Vector3 maximum,const Mat4& model) {
        for(float value:model.Data)if(!std::isfinite(value))return;
        const double low[]={minimum.X,minimum.Y,minimum.Z},high[]={maximum.X,maximum.Y,maximum.Z};
        for(std::size_t axis=0;axis<3;++axis) {
            if(!std::isfinite(low[axis])||!std::isfinite(high[axis])||low[axis]>high[axis])return;
            Center[axis]=(low[axis]+high[axis])*.5;Extent[axis]=(high[axis]-low[axis])*.5;
            Weight[axis]=std::fabs(Center[axis])+Extent[axis];
        }
        std::memcpy(Model,model.Data,sizeof(Model));Valid=true;
    }
};
class Frustum {
public:
    explicit Frustum(const Mat4& viewProjection) {
        for(float value:viewProjection.Data)if(!std::isfinite(value))_Valid=false;
        for(std::size_t plane=0;plane<6;++plane)for(std::size_t column=0;column<4;++column) {
            if(plane==4)_Planes[plane][column]=viewProjection(2,column);
            else {
                const std::size_t row=plane==5?2:plane/2;
                const double sign=plane==5||plane%2?-1.0:1.0;
                _Planes[plane][column]=double(viewProjection(3,column))+sign*viewProjection(row,column);
            }
        }
    }
    bool Intersects(Vector3 minimum,Vector3 maximum,const Mat4& model) const {
        return Intersects(Box(minimum,maximum,model));
    }
    bool Intersects(const Box& box) const {
        if(!_Valid||!box.Valid)return true;
        // Pull each world-space plane through Model. Its support on the original
        // local AABB handles rotations, nonuniform/mirrored scales and shears
        // without assuming that Model's axes are orthogonal.
        for(const auto& plane:_Planes) {
            double local[4]{},magnitude=0;
            for(std::size_t column=0;column<4;++column) {
                const float* matrix=box.Model+column*4;
                for(std::size_t row=0;row<4;++row) {
                    const double product=plane[row]*matrix[row];
                    local[column]+=product;
                    magnitude+=std::fabs(product)*box.Weight[column];
                }
            }
            double distance=local[3],radius=0;
            for(std::size_t axis=0;axis<3;++axis) {
                distance+=local[axis]*box.Center[axis];
                radius+=std::fabs(local[axis])*box.Extent[axis];
            }
            if(!std::isfinite(distance)||!std::isfinite(radius)||!std::isfinite(magnitude))return true;
            const double guard=32.0*std::numeric_limits<float>::epsilon()*(magnitude+radius+1.0);
            if(distance+radius < -guard)return false;
        }
        return true;
    }
private:
    double _Planes[6][4]{};
    bool _Valid=true;
};
}
