#include "../Renderer/RuntimeFrustum.h"
#include "Math/Projection.h"
#include "Math/TransformMath.h"
#include <iostream>
#include <cstdint>
#include <limits>
#include <stdexcept>

using namespace GameSproutRuntime;
namespace {
void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
bool Visible(const Mat4& projection,Vector3 position,Vector3 extent={.1f,.1f,.1f}){
    return RenderFrustum::Frustum(projection).Intersects(-extent,extent,RuntimeMath::ComposeTRS(position,{}, {1,1,1}));
}
// Previous scalar support/guard calculation is the numerical reference. The
// cached path must retain its rejection decisions, including mirrored/sheared
// transforms and large cancellation, rather than silently switch to a center
// test or to a world AABB with different edge behavior.
bool ReferenceIntersects(const Mat4& viewProjection,Vector3 minimum,Vector3 maximum,const Mat4& model) {
    for(float value:viewProjection.Data)if(!std::isfinite(value))return true;
    for(float value:model.Data)if(!std::isfinite(value))return true;
    const double low[]={minimum.X,minimum.Y,minimum.Z},high[]={maximum.X,maximum.Y,maximum.Z};
    double center[3],extent[3];
    for(unsigned axis=0;axis<3;++axis) {
        if(!std::isfinite(low[axis])||!std::isfinite(high[axis])||low[axis]>high[axis])return true;
        center[axis]=(low[axis]+high[axis])*.5;extent[axis]=(high[axis]-low[axis])*.5;
    }
    for(unsigned index=0;index<6;++index) {
        double plane[4];
        for(unsigned column=0;column<4;++column) {
            if(index==4)plane[column]=viewProjection(2,column);
            else plane[column]=double(viewProjection(3,column))+(index==5||index%2?-1.0:1.0)*viewProjection(index==5?2:index/2,column);
        }
        double local[4]{},magnitude=0;
        for(unsigned column=0;column<4;++column)for(unsigned row=0;row<4;++row) {
            const double product=plane[row]*model(row,column);local[column]+=product;
            magnitude+=std::fabs(product)*(column<3?std::fabs(center[column])+extent[column]:1.0);
        }
        double distance=local[3],radius=0;
        for(unsigned axis=0;axis<3;++axis){distance+=local[axis]*center[axis];radius+=std::fabs(local[axis])*extent[axis];}
        if(!std::isfinite(distance)||!std::isfinite(radius)||!std::isfinite(magnitude))return true;
        if(distance+radius < -32.0*std::numeric_limits<float>::epsilon()*(magnitude+radius+1.0))return false;
    }
    return true;
}
}
int main(){
    try {
        Mat4 perspective;Check(TryMakePerspective(Pi*.5f,1,1,10,perspective),"Cannot build perspective fixture");
        Check(Visible(perspective,{0,0,-3}),"A centered box was culled");
        Check(!Visible(perspective,{0,0,3}),"A box behind the camera was retained");
        Check(!Visible(perspective,{0,0,-.2f},{.05f,.05f,.05f}),"Zero-to-one near plane was treated as minus-one-to-one");
        Check(!Visible(perspective,{0,0,-12}),"A box beyond far plane was retained");
        Check(Visible(perspective,{0,0,-1},{.2f,.2f,.2f}),"A near-plane crossing box was culled");
        Check(Visible(perspective,{0,0,-10},{.2f,.2f,.2f}),"A far-plane crossing box was culled");
        Check(Visible(perspective,{0,0,-1},{0,0,0})&&Visible(perspective,{0,0,-10},{0,0,0}),"A box tangent to near/far plane was culled");
        Check(!Visible(perspective,{4,0,-3})&&!Visible(perspective,{-4,0,-3})&&
              !Visible(perspective,{0,4,-3})&&!Visible(perspective,{0,-4,-3}),"A side plane was omitted");
        Check(Visible(perspective,{3,0,-3},{0,0,0}),"A box tangent to the right plane was culled");
        Check(Visible(perspective,{0,0,0},{20,20,20}),"A box enclosing the entire frustum was culled");
        Mat4 wide;Check(TryMakePerspective(Pi*.5f,2,1,10,wide),"Cannot build wide projection");
        Check(!Visible(perspective,{5,0,-3})&&Visible(wide,{5,0,-3}),"Actual viewport aspect did not change the horizontal frustum");

        Mat4 ortho;Check(TryMakeOrthographic(2,1,1,10,ortho),"Cannot build orthographic fixture");
        Check(Visible(ortho,{0,0,-3})&&!Visible(ortho,{1.2f,0,-3}),"Orthographic side plane was incorrect");
        Check(Visible(ortho,{1.1f,0,-3},{.1f,.1f,.1f}),"A box tangent to the orthographic boundary was culled");
        const RenderFrustum::Frustum frustum(ortho);
        const Quaternion quarterTurn{0,0,.70710678f,.70710678f};
        Check(!frustum.Intersects({-1,-1,-1},{1,1,1},RuntimeMath::ComposeTRS({2,0,-3},{},{-.1f,2,1})),"A mirrored box wholly outside should be culled");
        Check(frustum.Intersects({-1,-1,-1},{1,1,1},RuntimeMath::ComposeTRS({2,0,-3},quarterTurn,{-.1f,2,1})),"Rotation/nonuniform negative scale was reduced to the box center");
        const Quaternion eighthTurn{0,0,.38268343f,.92387953f};
        Check(frustum.Intersects({-1,-1,-1},{1,1,1},RuntimeMath::ComposeTRS({2,0,-3},eighthTurn,{2,.2f,1})),"A rotated long box intersecting the frustum was culled");
        Check(!frustum.Intersects({-1,-1,-1},{1,1,1},RuntimeMath::ComposeTRS({3,0,-3},eighthTurn,{2,.2f,1})),"A fully outside rotated long box was retained");
        Check(frustum.Intersects({10,-.1f,-.1f},{12,.1f,.1f},RuntimeMath::ComposeTRS({-11,0,-3},{},{1,1,1})),"Local box center was confused with the transform origin");
        Mat4 shear;shear(0,1)=3;shear(0,3)=2;shear(2,3)=-3;
        Check(frustum.Intersects({-1,-1,-1},{1,1,1},shear),"A parent-induced shear was incorrectly rejected");
        shear(0,3)=6;Check(!frustum.Intersects({-1,-1,-1},{1,1,1},shear),"A wholly outside sheared box was retained");
        Mat4 camera;Check(TryMakeLookAt({10,0,0},{0,0,0},{0,1,0},camera),"Cannot aim explicit camera");
        const RenderFrustum::Frustum cameraFrustum(perspective*camera);
        Check(cameraFrustum.Intersects({4.9f,-.1f,-.1f},{5.1f,.1f,.1f},{}),"A rotated/transformed viewport camera rejected its visible box");
        Check(!cameraFrustum.Intersects({10.9f,-.1f,-.1f},{11.1f,.1f,.1f},{}),"A rotated/transformed viewport camera retained a box behind it");
        Mat4 invalid=ortho;invalid(2,2)=std::numeric_limits<float>::quiet_NaN();
        Check(RenderFrustum::Frustum(invalid).Intersects({100,100,100},{101,101,101},{}),"Invalid/uncertain planes must remain conservative");
        Check(frustum.Intersects({1,1,1},{-1,-1,-1},{}),"Invalid bounds must remain conservative");
        std::uint32_t seed=0x68fa3159u;
        const auto number=[&](){seed=seed*1664525u+1013904223u;return float(int((seed>>8)%20001u)-10000)/1000;};
        for(unsigned index=0;index<2048;++index) {
            Mat4 model;for(unsigned column=0;column<3;++column)for(unsigned row=0;row<3;++row)model(row,column)=number();
            model(0,3)=number()*2;model(1,3)=number()*2;model(2,3)=number()*2;
            const Vector3 center{number(),number(),number()},extent{std::fabs(number()),std::fabs(number()),std::fabs(number())};
            const RenderFrustum::Box box(center-extent,center+extent,model);
            for(const auto& projection:{ortho,perspective,perspective*camera})
                Check(RenderFrustum::Frustum(projection).Intersects(box)==ReferenceIntersects(projection,center-extent,center+extent,model),
                    "Cached affine support changed a scalar reference rejection/guard decision");
        }
        std::cout<<"PASS conservative canonical frustum: six planes, perspective/ortho, tangent/crossing/enclosing boxes, aspect/camera, mirrored/nonuniform/sheared transforms\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';return 1;}
}
