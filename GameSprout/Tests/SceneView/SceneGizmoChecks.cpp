#include "SceneGizmo.h"
#include "Math/Projection.h"
#include "Math/TransformMath.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using GameSproutRuntime::Vector2;
using GameSproutRuntime::Vector3;
using GameSproutRuntime::Vector4;
using GameSproutRuntime::Quaternion;
using GameSproutRuntime::Mat4;

static void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static bool Near(Vector3 a,Vector3 b,float epsilon=.0003f){return(a-b).Length()<epsilon;}
static bool Exact(const SceneGizmoTransform& a,const SceneGizmoTransform& b) {
    return a.Position.X==b.Position.X&&a.Position.Y==b.Position.Y&&a.Position.Z==b.Position.Z&&
        a.Rotation.X==b.Rotation.X&&a.Rotation.Y==b.Rotation.Y&&a.Rotation.Z==b.Rotation.Z&&a.Rotation.W==b.Rotation.W&&
        a.Scale.X==b.Scale.X&&a.Scale.Y==b.Scale.Y&&a.Scale.Z==b.Scale.Z;
}
static Vector3 Point(const Mat4& matrix,Vector3 value){const auto point=matrix*Vector4{value.X,value.Y,value.Z,1};return{point.X,point.Y,point.Z};}
static Vector3 Direction(const Mat4& matrix,Vector3 value){const auto point=matrix*Vector4{value.X,value.Y,value.Z,0};return{point.X,point.Y,point.Z};}
static Quaternion AngleAxis(Vector3 axis,float degrees){const float angle=GameSproutRuntime::DegreesToRadians(degrees)*.5f;return{axis.X*std::sin(angle),axis.Y*std::sin(angle),axis.Z*std::sin(angle),std::cos(angle)};}
static Quaternion Multiply(Quaternion a,Quaternion b) {
    return {a.W*b.X+a.X*b.W+a.Y*b.Z-a.Z*b.Y,a.W*b.Y-a.X*b.Z+a.Y*b.W+a.Z*b.X,
        a.W*b.Z+a.X*b.Y-a.Y*b.X+a.Z*b.W,a.W*b.W-a.X*b.X-a.Y*b.Y-a.Z*b.Z};
}
static Mat4 Matrix(const SceneGizmoTransform& value){return GameSproutRuntime::RuntimeMath::ComposeTRS(value.Position,value.Rotation,value.Scale);}
static void CheckMatrix(const Mat4& a,const Mat4& b,const char* message){for(int index=0;index<16;++index)Check(std::fabs(a.Data[index]-b.Data[index])<.001f,message);}
static SceneGizmoView View(Vector3 position={4,3,8},Vector3 target={}) {
    SceneGizmoView result;result.Width=1000;result.Height=800;result.Camera.Position=position;
    Check(GameSproutRuntime::TryMakeLookAt(position,target,{0,1,0},result.Camera.View),"Test view construction failed");return result;
}
static Vector2 Pixel(const SceneGizmoView& view,Vector3 point){Vector2 result;Check(SceneGizmo::Project(view,point,result),"Test point cannot be projected");return result;}
static void Setup(SceneGizmo& gizmo,const SceneGizmoTarget& target,const SceneGizmoView& view,const SceneGizmoSettings& settings) {
    std::string error;Check(gizmo.Update(target,view,settings,error),error.c_str());
}
static Vector2 AxisPress(SceneGizmo& gizmo,const SceneGizmoView& view,int axis) {
    const auto point=Pixel(view,gizmo.GetPivotWorld()+gizmo.GetAxis(axis)*(gizmo.GetHandleWorldSize()*.75f));
    const SceneGizmoHandle handles[]={SceneGizmoHandle::X,SceneGizmoHandle::Y,SceneGizmoHandle::Z};
    Check(gizmo.Hover(point)==handles[axis],"Axis handle did not match its projected geometry");return point;
}
static void CheckProjectionAndSize() {
    auto view=View({0,0,10});const auto pixel=Pixel(view,{2,1,0});GameSproutRuntime::Ray ray;
    Check(SceneGizmo::MakeRay(view,pixel,ray),"Ray creation failed");
    Check(Near(ray.GetPoint(-ray.Origin.Z/ray.Direction.Z),{2,1,0}),"Perspective project/ray mismatch");
    SceneGizmo targetGizmo;SceneGizmoTarget target;SceneGizmoSettings settings;
    Setup(targetGizmo,target,view,settings);const float first=targetGizmo.GetHandleWorldSize();
    view=View({0,0,20});Setup(targetGizmo,target,view,settings);
    Check(std::fabs(targetGizmo.GetHandleWorldSize()/first-2)<.0001f,"Perspective gizmo does not retain constant screen size");
    view.Camera.Projection.Projection=GameSproutRuntime::CameraProjection::Orthographic;view.Camera.Projection.OrthographicHeight=12;
    Setup(targetGizmo,target,view,settings);const float orthographic=targetGizmo.GetHandleWorldSize();
    const auto orthoPixel=Pixel(view,{2,1,0});Check(SceneGizmo::MakeRay(view,orthoPixel,ray),"Orthographic ray creation failed");
    Check(Near(ray.GetPoint(-ray.Origin.Z/ray.Direction.Z),{2,1,0}),"Orthographic project/ray mismatch");
    view=View({0,0,40});view.Camera.Projection.Projection=GameSproutRuntime::CameraProjection::Orthographic;view.Camera.Projection.OrthographicHeight=12;
    Setup(targetGizmo,target,view,settings);Check(std::fabs(targetGizmo.GetHandleWorldSize()-orthographic)<.0001f,"Orthographic handle size depends on camera distance");
    target.ParentWorld=GameSproutRuntime::RuntimeMath::ComposeTRS({},Quaternion{},{1,0,1});std::string error;
    Check(!targetGizmo.Update(target,view,settings,error),"Singular parent was accepted");
    target.ParentWorld=Mat4{};target.Local.Position.Z=100;
    Check(!targetGizmo.Update(target,view,settings,error),"Pivot behind the camera was accepted");
}
static void CheckMoveAndSnapshot() {
    SceneGizmo gizmo;SceneGizmoTarget target;SceneGizmoSettings settings;const auto view=View();
    target.ParentWorld=GameSproutRuntime::RuntimeMath::ComposeTRS({1,-1,0},AngleAxis({0,1,0},25),{2,3,4});
    target.Local.Position={.2f,.3f,.1f};target.Local.Scale={-1,2,3};settings.TranslationSnap=.5f;
    Setup(gizmo,target,view,settings);const Vector2 begin=AxisPress(gizmo,view,0);const auto pivot=gizmo.GetPivotWorld();const float length=gizmo.GetHandleWorldSize();
    std::string error;Check(gizmo.BeginDrag(begin,error),error.c_str());SceneGizmoTransform candidate;
    const auto end=Pixel(view,pivot+Vector3{length*.75f+1.37f,0,0});
    Check(gizmo.Drag(end,false,candidate,error),error.c_str());
    const Vector3 origin=Point(target.ParentWorld*Matrix(target.Local),{}),changed=Point(target.ParentWorld*Matrix(candidate),{});
    Check(Near(changed-origin,{1.37f,0,0}),"World axis movement was added directly to local position");
    Check(Near(candidate.Scale,target.Local.Scale),"Move changed negative scale");
    const auto unSnapped=candidate;
    Check(gizmo.Drag(end,true,candidate,error),error.c_str());
    Check(Near(Point(target.ParentWorld*Matrix(candidate),{})-origin,{1.5f,0,0}),"Translation snapping is not relative to drag start");
    SceneGizmoTarget accidental=target;accidental.Local.Position={100,100,100};
    Check(gizmo.Update(accidental,view,settings,error),"Drag snapshot update was rejected");
    Check(gizmo.Drag(end,false,candidate,error)&&Near(candidate.Position,unSnapped.Position),"Preview accumulated drift or replaced initial target");
    const auto beforeInvalid=candidate;
    Check(!gizmo.Drag({std::numeric_limits<float>::infinity(),0},false,candidate,error)&&Near(candidate.Position,beforeInvalid.Position),"Failed drag modified caller output");
    gizmo.CancelDrag();Check(!gizmo.IsDragging()&&Near(gizmo.GetPivotWorld(),pivot),"Cancel did not clear drag and restore initial visual pivot");

    // A planar constraint snaps each active axis, without allowing motion on its normal.
    target=SceneGizmoTarget{};settings.TranslationSnap=.25f;Setup(gizmo,target,view,settings);
    const float planeLength=gizmo.GetHandleWorldSize();const Vector3 planePoint=Vector3{.32f,.32f,0}*planeLength;
    const auto planePixel=Pixel(view,planePoint);Check(gizmo.Hover(planePixel)==SceneGizmoHandle::XY,"XY plane is not hittable");
    Check(gizmo.BeginDrag(planePixel,error),error.c_str());
    Check(gizmo.Drag(Pixel(view,planePoint+Vector3{.82f,.61f,0}),true,candidate,error),error.c_str());
    Check(Near(candidate.Position,{.75f,.5f,0}),"Planar movement constraint/snapping failed");gizmo.EndDrag();
}
static void CheckScaleAndCenter() {
    SceneGizmo gizmo;SceneGizmoTarget target;SceneGizmoSettings settings;auto view=View();
    target.ParentWorld=GameSproutRuntime::RuntimeMath::ComposeTRS({1,0,0},AngleAxis({0,1,0},15),{2,3,4});
    target.ParentRotation=AngleAxis({0,1,0},15);
    target.Local.Position={.1f,.2f,0};target.Local.Rotation=AngleAxis({0,0,1},35);target.Local.Scale={-2,3,4};
    settings.Tool=SceneGizmoTool::Scale;settings.Space=SceneGizmoSpace::Local;settings.Pivot=SceneGizmoPivot::Center;
    const Vector3 meshCenter{.3f,.2f,.1f};target.BoundsCenterWorld=Point(target.ParentWorld*Matrix(target.Local),meshCenter);target.HasBoundsCenter=true;
    Setup(gizmo,target,view,settings);const auto begin=AxisPress(gizmo,view,0);const float length=gizmo.GetHandleWorldSize();const auto pivot=gizmo.GetPivotWorld(),axis=gizmo.GetAxis(0);
    std::string error;Check(gizmo.BeginDrag(begin,error),error.c_str());SceneGizmoTransform candidate;
    Check(gizmo.Drag(Pixel(view,pivot+axis*(length*1.29f)),true,candidate,error),error.c_str());
    Check(Near(candidate.Scale,{-3,3,4}),"Local scale multiplier snap/negative sign failed");
    Check(Near(Point(target.ParentWorld*Matrix(candidate),meshCenter),target.BoundsCenterWorld),"Center scaling moved the actual geometry center");
    gizmo.EndDrag();

    // World-axis stretching of a rotated object is generally shear; it must not be approximated as TRS.
    target=SceneGizmoTarget{};target.Local.Rotation=AngleAxis({0,0,1},45);settings.Space=SceneGizmoSpace::World;settings.Pivot=SceneGizmoPivot::Pivot;
    Setup(gizmo,target,view,settings);const auto worldBegin=AxisPress(gizmo,view,0);const auto worldPivot=gizmo.GetPivotWorld();const float worldLength=gizmo.GetHandleWorldSize();
    Check(gizmo.BeginDrag(worldBegin,error),error.c_str());candidate.Position={42,43,44};
    Check(!gizmo.Drag(Pixel(view,worldPivot+Vector3{worldLength*1.25f,0,0}),false,candidate,error)&&Near(candidate.Position,{42,43,44}),"World scale silently discarded shear");
    gizmo.EndDrag();

    target=SceneGizmoTarget{};Setup(gizmo,target,view,settings);const auto center=Pixel(view,gizmo.GetPivotWorld());
    Check(gizmo.Hover(center)==SceneGizmoHandle::Center&&gizmo.BeginDrag(center,error),"Uniform center scale unavailable");
    Mat4 inverseView;Check(GameSproutRuntime::RuntimeMath::TryInverseAffine(view.Camera.View,inverseView),"Test inverse view failed");
    Vector3 diagonal=Direction(inverseView,{1,1,0});Check(diagonal.TryNormalize(diagonal),"Test diagonal failed");
    Check(gizmo.Drag(Pixel(view,gizmo.GetPivotWorld()+diagonal*(gizmo.GetHandleWorldSize()*.5f)),false,candidate,error),error.c_str());
    Check(Near(candidate.Scale,{1.5f,1.5f,1.5f}),"Uniform scale did not use the camera-facing constraint plane");gizmo.EndDrag();
}
static Vector3 FindRingPoint(SceneGizmo& gizmo,const SceneGizmoView& view,SceneGizmoHandle handle) {
    const int axis=handle==SceneGizmoHandle::X?0:handle==SceneGizmoHandle::Y?1:2;
    for(int step=1;step<90;++step) {
        const float angle=float(step)*GameSproutRuntime::Pi/45;
        const auto point=gizmo.GetPivotWorld()+(gizmo.GetAxis((axis+1)%3)*std::cos(angle)+gizmo.GetAxis((axis+2)%3)*std::sin(angle))*gizmo.GetHandleWorldSize();
        if(gizmo.Hover(Pixel(view,point))==handle)return point;
    }
    throw std::runtime_error("No distinct projected rotation ring point");
}
static void CheckRotation() {
    SceneGizmo gizmo;SceneGizmoTarget target;SceneGizmoSettings settings;settings.Tool=SceneGizmoTool::Rotate;
    const auto view=View();Setup(gizmo,target,view,settings);
    const Vector3 ring=FindRingPoint(gizmo,view,SceneGizmoHandle::Z);std::string error;
    Check(gizmo.BeginDrag(Pixel(view,ring),error),error.c_str());
    const Mat4 rotation=GameSproutRuntime::RuntimeMath::ComposeTRS({},AngleAxis({0,0,1},56),{1,1,1});SceneGizmoTransform candidate;
    Check(gizmo.Drag(Pixel(view,Direction(rotation,ring)),true,candidate,error),error.c_str());
    CheckMatrix(Matrix(candidate),GameSproutRuntime::RuntimeMath::ComposeTRS({},AngleAxis({0,0,1},60),{1,1,1}),"Rotation angle/snapping mismatch");gizmo.EndDrag();

    target.ParentWorld=GameSproutRuntime::RuntimeMath::ComposeTRS({},Quaternion{},{2,3,4});
    Setup(gizmo,target,view,settings);const auto worldRing=FindRingPoint(gizmo,view,SceneGizmoHandle::Z);
    Check(gizmo.BeginDrag(Pixel(view,worldRing),error),error.c_str());candidate.Position={42,43,44};
    Check(!gizmo.Drag(Pixel(view,Direction(rotation,worldRing)),false,candidate,error)&&Near(candidate.Position,{42,43,44}),"World rotation under nonuniform parent silently discarded local shear");gizmo.EndDrag();
    settings.Space=SceneGizmoSpace::Local;settings.Pivot=SceneGizmoPivot::Center;target.HasBoundsCenter=true;
    const Vector3 centerLocal{.4f,.2f,0};target.BoundsCenterWorld=Point(target.ParentWorld*Matrix(target.Local),centerLocal);
    Setup(gizmo,target,view,settings);const auto localRing=FindRingPoint(gizmo,view,SceneGizmoHandle::Z),pivot=gizmo.GetPivotWorld();
    Check(gizmo.BeginDrag(Pixel(view,localRing),error),error.c_str());
    Check(gizmo.Drag(Pixel(view,pivot+Direction(rotation,localRing-pivot)),false,candidate,error),error.c_str());
    Check(Near(Point(target.ParentWorld*Matrix(candidate),centerLocal),target.BoundsCenterWorld),"Local rotation under nonuniform parent moved the center");
    CheckMatrix(GameSproutRuntime::RuntimeMath::ComposeTRS({},candidate.Rotation,{1,1,1}),rotation,"Local rotation did not preserve exact TRS");gizmo.EndDrag();

    // Edge-on rings use a finite camera-facing tangent constraint instead of an undefined ray-plane intersection.
    const auto straightView=View({0,0,10});target=SceneGizmoTarget{};settings.Space=SceneGizmoSpace::World;settings.Pivot=SceneGizmoPivot::Pivot;
    Setup(gizmo,target,straightView,settings);const auto edgeRing=FindRingPoint(gizmo,straightView,SceneGizmoHandle::X);
    Check(gizmo.BeginDrag(Pixel(straightView,edgeRing),error),"Edge-on ring could not start a safe drag");
    Check(gizmo.Drag(Pixel(straightView,edgeRing+Vector3{0,.3f,0}),false,candidate,error),"Edge-on ring tangent drag failed");gizmo.EndDrag();
}
static void CheckGeometry() {
    SceneGizmo gizmo;SceneGizmoTarget target;SceneGizmoSettings settings;const auto view=View();
    for(const auto tool:{SceneGizmoTool::Move,SceneGizmoTool::Rotate,SceneGizmoTool::Scale}) {
        settings.Tool=tool;Setup(gizmo,target,view,settings);GameSproutRuntime::DebugDrawList geometry;gizmo.BuildDrawList(geometry);
        Check(!geometry.Lines.empty(),"Gizmo debug geometry is empty");
        if(tool!=SceneGizmoTool::Rotate)Check(!geometry.Triangles.empty(),"Move arrows/scale boxes are missing solid caps");
        for(const auto& line:geometry.Lines)Check(!line.DepthTest,"Gizmo line is hidden behind world geometry");
        for(const auto& triangle:geometry.Triangles)Check(!triangle.DepthTest,"Gizmo triangle is hidden behind world geometry");
    }
}
static void CheckNoOpPreservesOriginalTRS() {
    const auto view=View();SceneGizmo gizmo;SceneGizmoTarget target;SceneGizmoSettings settings;std::string error;
    target.Local.Position={.1234567f,-.2345678f,.3456789f};target.Local.Scale={-1,2,3};
    target.Local.Rotation=AngleAxis({0,0,1},27);
    // Normalization is needed for math, but must never overwrite an untouched transaction snapshot.
    target.Local.Rotation.X*=2;target.Local.Rotation.Y*=2;target.Local.Rotation.Z*=2;target.Local.Rotation.W*=2;
    SceneGizmoTransform candidate;
    settings.Tool=SceneGizmoTool::Rotate;Setup(gizmo,target,view,settings);
    const auto ring=FindRingPoint(gizmo,view,SceneGizmoHandle::Z),pivot=gizmo.GetPivotWorld();const auto pixel=Pixel(view,ring);
    Check(Exact(gizmo.GetInitialTransform(),target.Local),"Update normalized the original TRS snapshot");
    Check(gizmo.BeginDrag(pixel,error),"Mirror zero-click begin failed");
    Check(gizmo.Drag(pixel,false,candidate,error),"Mirror zero-click drag failed");
    Check(Exact(candidate,target.Local),"World rotation zero-click rewrote mirrored TRS");
    const Mat4 tinyRotation=GameSproutRuntime::RuntimeMath::ComposeTRS({},AngleAxis({0,0,1},2),{1,1,1});
    Check(gizmo.Drag(Pixel(view,pivot+Direction(tinyRotation,ring-pivot)),true,candidate,error)&&Exact(candidate,target.Local),"Snapped zero rotation rewrote mirrored TRS");gizmo.EndDrag();

    settings.Tool=SceneGizmoTool::Scale;Setup(gizmo,target,view,settings);const auto axisPixel=AxisPress(gizmo,view,0);const float length=gizmo.GetHandleWorldSize();
    Check(gizmo.BeginDrag(axisPixel,error)&&gizmo.Drag(axisPixel,false,candidate,error)&&Exact(candidate,target.Local),"World scale zero-click canonicalized mirrored TRS");
    Check(gizmo.Drag(Pixel(view,gizmo.GetPivotWorld()+gizmo.GetAxis(0)*(length*.77f)),true,candidate,error)&&Exact(candidate,target.Local),"Snapped unit scale rewrote mirrored TRS");gizmo.EndDrag();

    target.ParentRotation=AngleAxis({0,1,0},17);
    target.ParentWorld=GameSproutRuntime::RuntimeMath::ComposeTRS({.7f,-.2f,.1f},target.ParentRotation,{-2,3,4});
    target.HasBoundsCenter=true;target.BoundsCenterWorld=Point(target.ParentWorld*Matrix(target.Local),{.17f,.29f,.31f});
    settings.Space=SceneGizmoSpace::Local;settings.Pivot=SceneGizmoPivot::Center;
    for(const auto tool:{SceneGizmoTool::Rotate,SceneGizmoTool::Scale}) {
        settings.Tool=tool;Setup(gizmo,target,view,settings);const auto originalPivot=gizmo.GetPivotWorld();
        const auto begin=tool==SceneGizmoTool::Rotate?Pixel(view,FindRingPoint(gizmo,view,SceneGizmoHandle::Z)):AxisPress(gizmo,view,0);
        Check(gizmo.BeginDrag(begin,error)&&gizmo.Drag(begin,false,candidate,error)&&Exact(candidate,target.Local),"Local Center zero-click introduced position rounding or normalized original rotation");
        gizmo.CancelDrag();Check(!gizmo.IsDragging()&&Exact(gizmo.GetInitialTransform(),target.Local)&&Near(gizmo.GetPivotWorld(),originalPivot),"Cancel lost the untouched Center snapshot");
    }
}
static void CheckLocalToolsUnderShearedParent() {
    const auto view=View();SceneGizmo gizmo;SceneGizmoTarget target;SceneGizmoSettings settings;std::string error;
    const Quaternion grandparentRotation=AngleAxis({0,1,0},10),parentRotation=AngleAxis({0,0,1},45);
    target.ParentWorld=GameSproutRuntime::RuntimeMath::ComposeTRS({},grandparentRotation,{-2,1,3})*
        GameSproutRuntime::RuntimeMath::ComposeTRS({},parentRotation,{1,1,1});
    target.ParentRotation=Multiply(grandparentRotation,parentRotation);
    target.Local.Position={.05f,.05f,.02f};target.Local.Rotation=AngleAxis({1,0,0},20);target.Local.Scale={-1,2,3};
    Vector3 decomposedPosition,decomposedScale;Quaternion decomposedRotation;
    Check(!GameSproutRuntime::RuntimeMath::TryDecomposeTRS(target.ParentWorld,decomposedPosition,decomposedRotation,decomposedScale),"Sheared-parent regression fixture is actually decomposable");
    settings.Space=SceneGizmoSpace::Local;Setup(gizmo,target,view,settings);
    const Mat4 expectedOrientation=GameSproutRuntime::RuntimeMath::ComposeTRS({},Multiply(target.ParentRotation,target.Local.Rotation),{1,1,1});
    Check(Near(gizmo.GetAxis(0),Direction(expectedOrientation,{1,0,0})),"Local axis orientation inferred rotation/reflection from a sheared matrix");
    const auto begin=AxisPress(gizmo,view,0);const auto pivot=gizmo.GetPivotWorld(),axis=gizmo.GetAxis(0);const float length=gizmo.GetHandleWorldSize();
    Check(gizmo.BeginDrag(begin,error),error.c_str());SceneGizmoTransform candidate;
    Check(gizmo.Drag(Pixel(view,pivot+axis*(length*1.05f)),false,candidate,error),error.c_str());
    Check(Near(Point(target.ParentWorld*Matrix(candidate),{})-Point(target.ParentWorld*Matrix(target.Local),{}),axis*(length*.3f)),"Local move under sheared parent failed inverse-parent conversion");gizmo.EndDrag();

    const Vector3 centerLocal{.15f,.10f,.05f};target.HasBoundsCenter=true;target.BoundsCenterWorld=Point(target.ParentWorld*Matrix(target.Local),centerLocal);
    settings.Pivot=SceneGizmoPivot::Center;settings.Tool=SceneGizmoTool::Scale;Setup(gizmo,target,view,settings);
    const auto scaleBegin=AxisPress(gizmo,view,0);const auto scalePivot=gizmo.GetPivotWorld(),scaleAxis=gizmo.GetAxis(0);const float scaleLength=gizmo.GetHandleWorldSize();
    Check(gizmo.BeginDrag(scaleBegin,error)&&gizmo.Drag(Pixel(view,scalePivot+scaleAxis*(scaleLength*1.15f)),false,candidate,error),"Local Center scale was disabled by sheared parent");
    Check(Near(candidate.Scale,{-1.4f,2,3})&&Near(Point(target.ParentWorld*Matrix(candidate),centerLocal),target.BoundsCenterWorld),"Local scale under sheared/reflected parent changed center or scale identity");gizmo.EndDrag();

    settings.Tool=SceneGizmoTool::Rotate;Setup(gizmo,target,view,settings);const auto ring=FindRingPoint(gizmo,view,SceneGizmoHandle::Z),rotationPivot=gizmo.GetPivotWorld();
    const Mat4 worldRotation=GameSproutRuntime::RuntimeMath::ComposeTRS({},AngleAxis(gizmo.GetAxis(2),23),{1,1,1});
    Check(gizmo.BeginDrag(Pixel(view,ring),error)&&gizmo.Drag(Pixel(view,rotationPivot+Direction(worldRotation,ring-rotationPivot)),false,candidate,error),"Local Center rotation was disabled by sheared parent");
    CheckMatrix(GameSproutRuntime::RuntimeMath::ComposeTRS({},candidate.Rotation,{1,1,1}),
        GameSproutRuntime::RuntimeMath::ComposeTRS({},Multiply(target.Local.Rotation,AngleAxis({0,0,1},23)),{1,1,1}),"Local rotation under sheared parent did not preserve quaternion semantics");
    Check(Near(Point(target.ParentWorld*Matrix(candidate),centerLocal),target.BoundsCenterWorld),"Local rotation under sheared parent moved Center");gizmo.EndDrag();
}
static void CheckMirroredNonzeroBranches() {
    const auto view=View();SceneGizmo gizmo;SceneGizmoTarget target;SceneGizmoSettings settings;std::string error;SceneGizmoTransform candidate;
    target.Local.Position={.1f,.2f,0};target.Local.Scale={-1,2,3};target.Local.Rotation=AngleAxis({0,0,1},27);
    settings.Tool=SceneGizmoTool::Rotate;Setup(gizmo,target,view,settings);
    const auto ring=FindRingPoint(gizmo,view,SceneGizmoHandle::Z),pivot=gizmo.GetPivotWorld();
    const Mat4 delta=GameSproutRuntime::RuntimeMath::ComposeTRS({},AngleAxis({0,0,1},120),{1,1,1});
    Check(gizmo.BeginDrag(Pixel(view,ring),error)&&gizmo.Drag(Pixel(view,pivot+Direction(delta,ring-pivot)),false,candidate,error),"Mirrored nonzero World rotation failed");
    Check(Near(candidate.Scale,target.Local.Scale),"Rotation beyond 90 degrees changed scale signs to choose a nearer quaternion");
    SceneGizmoTransform expected=target.Local;expected.Rotation=Multiply(AngleAxis({0,0,1},120),target.Local.Rotation);
    CheckMatrix(Matrix(candidate),Matrix(expected),"Equivalent-branch selection altered the requested World rotation");gizmo.EndDrag();

    target.Local.Rotation={};settings.Tool=SceneGizmoTool::Scale;Setup(gizmo,target,view,settings);
    const auto begin=AxisPress(gizmo,view,0);const auto scalePivot=gizmo.GetPivotWorld();const float length=gizmo.GetHandleWorldSize();
    Check(gizmo.BeginDrag(begin,error)&&gizmo.Drag(Pixel(view,scalePivot+Vector3{length*1.15f,0,0}),false,candidate,error),"Mirrored positive World scale failed");
    Check(Near(candidate.Scale,{-1.4f,2,3}),"Positive scaling moved the original negative sign to another axis");
    expected=target.Local;expected.Scale={-1.4f,2,3};CheckMatrix(Matrix(candidate),Matrix(expected),"Positive mirrored scale changed rotation");
    Check(gizmo.Drag(Pixel(view,scalePivot+Vector3{-length*.75f,0,0}),false,candidate,error),"Scaling across negative multiplier was rejected");
    expected.Scale={.5f,2,3};Check(Near(candidate.Scale,expected.Scale),"Negative multiplier did not allow determinant sign change on the requested axis");
    CheckMatrix(Matrix(candidate),Matrix(expected),"Negative multiplier equivalent branch changed the intended matrix");gizmo.EndDrag();
}
int main() {
    try {CheckProjectionAndSize();CheckMoveAndSnapshot();CheckScaleAndCenter();CheckRotation();CheckGeometry();CheckNoOpPreservesOriginalTRS();CheckLocalToolsUnderShearedParent();CheckMirroredNonzeroBranches();
        std::cout<<"PASS pure gizmo rays/projection, constant pixel size, axis/plane/center movement, local/world/center TRS, snapping, no-shear safety and overlay geometry\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
