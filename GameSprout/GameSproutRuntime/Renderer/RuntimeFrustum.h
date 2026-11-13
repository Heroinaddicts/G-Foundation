#pragma once
#include "Api/Math/Mat4.h"
#include "Api/Math/Vector3.h"
#include <cmath>
#include <limits>

namespace GameSproutRuntime::RenderFrustum {
// Canonical Runtime clip space: -w <= x,y <= w and 0 <= z <= w.
// A box is rejected only when its complete affine image is outside one plane.
// All calculations use double and a float-scale guard band; uncertain/invalid
// inputs are retained. No viewport or scene state is stored between frames.
struct Box {
    double Center[4]{};
    double Axis[3][4]{};
    double Magnitude[4]{};
    bool Valid=false;
    Box()=default;
    Box(Vector3 minimum,Vector3 maximum,const Mat4& model) {
        for(float value:model.Data)if(!std::isfinite(value))return;
        const double low[]={minimum.X,minimum.Y,minimum.Z},high[]={maximum.X,maximum.Y,maximum.Z};
        double center[3],extent[3];
        for(std::size_t axis=0;axis<3;++axis) {
            if(!std::isfinite(low[axis])||!std::isfinite(high[axis])||low[axis]>high[axis])return;
            center[axis]=(low[axis]+high[axis])*.5;
            extent[axis]=(high[axis]-low[axis])*.5;
        }
        for(std::size_t row=0;row<4;++row) {
            Center[row]=model(row,3);
            Magnitude[row]=std::fabs(Center[row]);
            for(std::size_t column=0;column<3;++column) {
                const double coefficient=model(row,column);
                Center[row]+=coefficient*center[column];
                Axis[column][row]=coefficient*extent[column];
                Magnitude[row]+=std::fabs(coefficient)*(std::fabs(center[column])+extent[column]);
            }
        }
        Valid=true;
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
            _AbsolutePlanes[plane][column]=std::fabs(_Planes[plane][column]);
        }
    }
    bool Intersects(Vector3 minimum,Vector3 maximum,const Mat4& model) const {
        return Intersects(Box(minimum,maximum,model));
    }
    bool Intersects(const Box& box) const {
        if(!_Valid||!box.Valid)return true;
        // Project the cached center and half axes. Support remains exact for
        // nonorthogonal, scaled or mirrored transforms.
        for(std::size_t index=0;index<6;++index) {
            const auto& plane=_Planes[index];
            const double distance=Dot(plane,box.Center);
            const double radius=std::fabs(Dot(plane,box.Axis[0]))+std::fabs(Dot(plane,box.Axis[1]))+
                std::fabs(Dot(plane,box.Axis[2]));
            if(distance+radius>=0)continue;
            const double magnitude=Dot(_AbsolutePlanes[index],box.Magnitude);
            const double guard=32.0*std::numeric_limits<float>::epsilon()*(magnitude+radius+1.0);
            if(distance+radius < -guard)return false;
        }
        return true;
    }
private:
    static double Dot(const double* left,const double* right) {
        return left[0]*right[0]+left[1]*right[1]+left[2]*right[2]+left[3]*right[3];
    }
    double _Planes[6][4]{};
    double _AbsolutePlanes[6][4]{};
    bool _Valid=true;
};
}
