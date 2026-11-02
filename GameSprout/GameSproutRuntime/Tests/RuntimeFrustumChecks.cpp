#include "../Renderer/RuntimeFrustum.h"
#include "Math/Projection.h"
#include "Math/TransformMath.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace GameSproutRuntime;
namespace {
void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
bool Visible(const Mat4& projection,Vector3 position,Vector3 extent={.1f,.1f,.1f}){
    return RenderFrustum::Frustum(projection).Intersects(-extent,extent,RuntimeMath::ComposeTRS(position,{}, {1,1,1}));
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
        std::cout<<"PASS conservative canonical frustum: six planes, perspective/ortho, tangent/crossing/enclosing boxes, aspect/camera, mirrored/nonuniform/sheared transforms\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';return 1;}
}
