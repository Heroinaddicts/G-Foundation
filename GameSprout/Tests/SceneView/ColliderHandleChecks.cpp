#include "ColliderHandleGeometry.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value,const char* error){if(!value)throw std::runtime_error(error);}
bool Near(float a,float b){return std::fabs(a-b)<.0001f;}
SceneToolContext Context() {
    SceneToolContext context;context.Frame.Width=800;context.Frame.Height=600;
    context.Frame.Camera.View(2,3)=-8;context.SnapDistance=.5f;return context;
}
SceneToolPointer Pointer(ColliderHandleGeometry& geometry,const SceneToolContext& context,unsigned handle,float x,float y) {
    SceneToolPointer result;Check(ColliderHandleGeometry::Project(context,geometry.HandlePosition(handle),result.Pixel),"Handle did not project");
    std::string error;Check(geometry.Begin(result.Pixel,error),error.c_str());result.Pixel.X+=x;result.Pixel.Y+=y;return result;
}
}
int main() {
    try {
        const auto context=Context();GameSproutRuntime::Mat4 world;std::string error;
        ColliderEditState state;state.Box.Size={2,4,6};ColliderHandleGeometry geometry;
        Check(geometry.Update(state,world,context,error),error.c_str());
        auto pointer=Pointer(geometry,context,1,80,0);ColliderEditState desired;
        Check(geometry.Drag(pointer,desired,error),error.c_str());
        Check(desired.Box.Size.X>2&&Near(desired.Settings.Center.X-desired.Box.Size.X*.5f,-1)
            &&Near(desired.Box.Size.Y,4)&&Near(desired.Box.Size.Z,6),"Box single face drag must preserve the opposite face and other axes");
        pointer.Alt=true;Check(geometry.Drag(pointer,desired,error),error.c_str());
        Check(Near(desired.Settings.Center.X,0)&&desired.Box.Size.X>2,"Alt resize did not pin the Collider center");
        pointer.Alt=false;pointer.Shift=true;Check(geometry.Drag(pointer,desired,error),error.c_str());
        Check(Near(desired.Box.Size.Y/desired.Box.Size.X,2)&&Near(desired.Box.Size.Z/desired.Box.Size.X,3),"Shift did not keep Box proportions");
        pointer.Shift=false;pointer.Snap=true;Check(geometry.Drag(pointer,desired,error),error.c_str());
        Check(Near((desired.Box.Size.X-2)/.5f,std::round((desired.Box.Size.X-2)/.5f)),"Collider size snap did not use world-space distance");
        geometry.End();
        state.Kind=ColliderShapeKind::Sphere;state.Sphere.Radius=.5f;
        world=GameSproutRuntime::RuntimeMath::ComposeTRS({},{},{-2,3,1});
        Check(geometry.Update(state,world,context,error),error.c_str());
        Check(Near(geometry.HandlePosition(1).Length(),1.5f),"Sphere handles used an ellipsoid instead of the largest scale");
        GameSproutRuntime::Vector2 first,second;ColliderHandleGeometry::Project(context,geometry.HandlePosition(0),first);ColliderHandleGeometry::Project(context,geometry.HandlePosition(1),second);
        GameSproutRuntime::Vector3 worldAxis;Check((geometry.HandlePosition(1)-geometry.HandlePosition(0)).TryNormalize(worldAxis),"Sphere axis");
        pointer=Pointer(geometry,context,1,second.X>first.X?30.f:-30.f,0);Check(geometry.Drag(pointer,desired,error),error.c_str());
        const auto moved=world*GameSproutRuntime::Vector4{desired.Settings.Center.X,desired.Settings.Center.Y,desired.Settings.Center.Z,1};
        Check(desired.Sphere.Radius>.5f&&worldAxis.Dot({moved.X,moved.Y,moved.Z})>0,"Mirrored sphere local Center did not map back to the dragged world face");
        geometry.End();
        state.Kind=ColliderShapeKind::Capsule;state.Capsule={.25f,2,GameSproutRuntime::PhysicsAxis::Y};
        world=GameSproutRuntime::RuntimeMath::ComposeTRS({},{},{2,3,4});
        Check(geometry.Update(state,world,context,error),error.c_str());
        Check(Near(geometry.HandlePosition(1).X,1)&&Near(geometry.HandlePosition(3).Y,3),"Capsule handles disagree with axial/radial world scaling");
        pointer=Pointer(geometry,context,3,0,300);Check(geometry.Drag(pointer,desired,error),error.c_str());
        Check(desired.Capsule.Height>=2*desired.Capsule.Radius,"Capsule height was allowed below its diameter");
        geometry.End();
        Check(geometry.Update(state,world,context,error),error.c_str());
        pointer=Pointer(geometry,context,1,150,0);Check(geometry.Drag(pointer,desired,error),error.c_str());
        Check(desired.Capsule.Radius>.25f&&desired.Capsule.Height>=2*desired.Capsule.Radius,"Capsule radius did not expand its height when required");
        pointer.Shift=true;Check(geometry.Drag(pointer,desired,error)&&Near(desired.Capsule.Height/desired.Capsule.Radius,8),"Shift did not keep capsule height/radius proportions");
        geometry.End();
        Check(geometry.Update(state,world,context,error),error.c_str());
        GameSproutRuntime::Vector2 pixel;Check(ColliderHandleGeometry::Project(context,geometry.HandlePosition(1),pixel),"Handle projection");
        Check(geometry.Begin(pixel,error),error.c_str());SceneToolPointer same;same.Pixel=pixel;
        Check(geometry.Drag(same,desired,error)&&Near(desired.Capsule.Radius,state.Capsule.Radius)
            &&Near(desired.Settings.Center.X,state.Settings.Center.X),"Click-release without movement changed Collider data");
        geometry.End();GameSproutRuntime::DebugDrawList draw;geometry.Draw(draw);
        Check(draw.Lines.size()==204&&draw.Triangles.size()==72,"Active Collider outline and six face handles were not drawn together");
        state.Settings.Enabled=false;Check(geometry.Update(state,world,context,error),error.c_str());draw={};geometry.Draw(draw);
        Check(draw.Lines.front().Tint.G<.8f,"Disabled Collider tools must use grey geometry");
        world=GameSproutRuntime::RuntimeMath::ComposeTRS({},{},{0,1,1});Check(!geometry.Update(state,world,context,error),"Singular transform accepted for editing");
        world={};world(0,1)=.5f;Check(!geometry.Update(state,world,context,error),"Sheared primitive transform accepted for editing");
        state.Kind=ColliderShapeKind::Mesh;world={};Check(!geometry.Update(state,world,context,error),"Mesh Collider incorrectly received primitive size handles");
        std::cout<<"PASS Collider handle face resize, center, Shift/Alt/snap, signed/nonuniform scale, capsule constraints and no-op clicks\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
