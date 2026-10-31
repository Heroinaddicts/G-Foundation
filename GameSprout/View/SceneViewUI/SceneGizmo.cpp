#include "SceneGizmo.h"
#include "SceneGizmoMath.h"

using GameSproutRuntime::Vector2;
using GameSproutRuntime::Vector3;
using GameSproutRuntime::Vector4;
using GameSproutRuntime::Quaternion;
using GameSproutRuntime::Mat4;
using GameSproutRuntime::Ray;

bool SceneGizmo::MakeRay(const SceneGizmoView& view,Vector2 pixel,Ray& output) {
    Mat4 projection,inverseView;
    if(!SceneGizmoDetail::Finite(pixel)||!SceneGizmoDetail::Projection(view,projection)||
        !GameSproutRuntime::RuntimeMath::TryInverseAffine(view.Camera.View,inverseView))return false;
    const float x=2*pixel.X/view.Width-1,y=1-2*pixel.Y/view.Height;
    Ray candidate;
    if(view.Camera.Projection.Projection==GameSproutRuntime::CameraProjection::Perspective) {
        candidate.Origin=SceneGizmoDetail::Point(inverseView,{});
        const Vector3 local{x/projection(0,0),y/projection(1,1),-1};
        if(!SceneGizmoDetail::Direction(inverseView,local).TryNormalize(candidate.Direction))return false;
    } else {
        candidate.Origin=SceneGizmoDetail::Point(inverseView,{x/projection(0,0),y/projection(1,1),-view.Camera.Projection.NearPlane});
        if(!SceneGizmoDetail::Direction(inverseView,{0,0,-1}).TryNormalize(candidate.Direction))return false;
    }
    if(!SceneGizmoDetail::Finite(candidate.Origin))return false;
    output=candidate;return true;
}

bool SceneGizmo::Project(const SceneGizmoView& view,Vector3 world,Vector2& output) {
    Mat4 projection;
    if(!SceneGizmoDetail::Finite(world)||!SceneGizmoDetail::Projection(view,projection))return false;
    const Vector4 clip=projection*(view.Camera.View*Vector4{world.X,world.Y,world.Z,1});
    if(!std::isfinite(clip.W)||clip.W<=1e-7f||!std::isfinite(clip.Z)||clip.Z<0||clip.Z>clip.W)return false;
    const Vector2 candidate{(clip.X/clip.W+1)*.5f*view.Width,(1-clip.Y/clip.W)*.5f*view.Height};
    if(!SceneGizmoDetail::Finite(candidate))return false;
    output=candidate;return true;
}

bool SceneGizmo::Update(const SceneGizmoTarget& target,const SceneGizmoView& view,const SceneGizmoSettings& settings,std::string& error) {
    error.clear();
    // 宿主每帧提供当前节点用于绘制；拖动时仍使用按下时的不可变快照。
    if(_Dragging)return true;
    _Valid=false;_Hovered=SceneGizmoHandle::None;
    const auto fail=[&](const char* text){error=text;return false;};
    Quaternion normalized;
    if(!SceneGizmoDetail::Finite(target.Local.Position)||!SceneGizmoDetail::Finite(target.Local.Scale)||
        !target.Local.Rotation.TryNormalize(normalized)||(target.HasBoundsCenter&&!SceneGizmoDetail::Finite(target.BoundsCenterWorld)))
        return fail("Gizmo target contains invalid TRS or bounds.");
    if(!std::isfinite(settings.SizePixels)||settings.SizePixels<16||settings.SizePixels>4096||
        !std::isfinite(settings.HitRadiusPixels)||settings.HitRadiusPixels<=0||settings.HitRadiusPixels>64||
        !std::isfinite(settings.TranslationSnap)||settings.TranslationSnap<0||
        !std::isfinite(settings.RotationSnapDegrees)||settings.RotationSnapDegrees<0||
        !std::isfinite(settings.ScaleSnap)||settings.ScaleSnap<0)
        return fail("Gizmo size or snap settings are invalid.");
    if(settings.Tool<SceneGizmoTool::Move||settings.Tool>SceneGizmoTool::Scale||
        settings.Space<SceneGizmoSpace::World||settings.Space>SceneGizmoSpace::Local||
        settings.Pivot<SceneGizmoPivot::Pivot||settings.Pivot>SceneGizmoPivot::Center)
        return fail("Unknown gizmo mode.");
    Mat4 inverseParent,inverseView,projection,world;
    if(!GameSproutRuntime::RuntimeMath::TryInverseAffine(target.ParentWorld,inverseParent))
        return fail("Cannot manipulate an object under a singular parent transform.");
    if(!SceneGizmoDetail::Projection(view,projection)||
        !GameSproutRuntime::RuntimeMath::TryInverseAffine(view.Camera.View,inverseView))return fail("Invalid gizmo camera.");
    try {world=target.ParentWorld*SceneGizmoDetail::Compose(target.Local);}catch(...){return fail("Invalid target transform matrix.");}
    if(!GameSproutRuntime::RuntimeMath::IsFinite(world))return fail("Target world transform overflows.");
    const Vector3 pivot=settings.Pivot==SceneGizmoPivot::Center&&target.HasBoundsCenter?target.BoundsCenterWorld:SceneGizmoDetail::Point(world,{});
    Vector2 screen;
    if(!Project(view,pivot,screen))return fail("Gizmo pivot is outside the camera depth range.");
    Vector3 right,up,forward;
    if(!SceneGizmoDetail::Direction(inverseView,{1,0,0}).TryNormalize(right)||
        !SceneGizmoDetail::Direction(inverseView,{0,1,0}).TryNormalize(up)||
        !SceneGizmoDetail::Direction(inverseView,{0,0,-1}).TryNormalize(forward)||
        std::fabs(right.Dot(up))>1e-5f||std::fabs(right.Dot(forward))>1e-5f||std::fabs(up.Dot(forward))>1e-5f)
        return fail("Gizmo camera basis is not orthogonal.");
    const Vector3 cameraPivot=SceneGizmoDetail::Point(view.Camera.View,pivot);
    const double worldPerPixel=view.Camera.Projection.Projection==GameSproutRuntime::CameraProjection::Perspective?
        2.0*(-cameraPivot.Z)*std::tan(view.Camera.Projection.VerticalFovRadians*.5)/view.Height:
        double(view.Camera.Projection.OrthographicHeight)/view.Height;
    const double length=worldPerPixel*settings.SizePixels;
    if(!std::isfinite(length)||length<=0||length>std::numeric_limits<float>::max())return fail("Gizmo screen size cannot be represented.");
    Vector3 axes[3]={{1,0,0},{0,1,0},{0,0,1}};
    if(settings.Space==SceneGizmoSpace::Local) {
        Quaternion parentRotation;
        if(!target.ParentRotation.TryNormalize(parentRotation))return fail("Invalid parent rotation for local gizmo axes.");
        const Mat4 orientation=GameSproutRuntime::RuntimeMath::ComposeTRS({},SceneGizmoDetail::Multiply(parentRotation,normalized),{1,1,1});
        for(int axis=0;axis<3;++axis)axes[axis]=SceneGizmoDetail::Direction(orientation,axes[axis]);
    }
    // 计算时可归一化四元数，事务快照必须逐项保留输入，零操作不能悄悄改值。
    _Target=target;_View=view;_Settings=settings;
    _World=world;_InverseParent=inverseParent;_Pivot=pivot;_DrawPivot=pivot;_Length=float(length);
    for(int axis=0;axis<3;++axis)_Axes[axis]=axes[axis];
    _CameraRight=right;_CameraUp=up;_CameraForward=forward;
    _Valid=true;return true;
}

SceneGizmoHandle SceneGizmo::Hover(Vector2 pixel) {
    if(!_Dragging)_Hovered=HitTest(pixel);
    return ActiveHandle();
}

bool SceneGizmo::BeginDrag(Vector2 pixel,std::string& error) {
    error.clear();if(_Dragging){error="A gizmo drag is already active.";return false;}
    const auto handle=HitTest(pixel);
    if(!_Valid||handle==SceneGizmoHandle::None){error="The pointer does not hit a gizmo handle.";return false;}
    Ray ray;if(!MakeRay(_View,pixel,ray)){error="Cannot construct a pointer ray.";return false;}
    const int axis=SceneGizmoDetail::AxisIndex(handle);int first=0,second=1;
    const bool plane=SceneGizmoDetail::PlaneAxes(handle,first,second);
    Vector3 normal,start,rotationAxis;bool linearRotation=false;
    if(_Settings.Tool==SceneGizmoTool::Rotate) {
        rotationAxis=axis>=0?_Axes[axis]:_CameraForward;
        // 从侧面看旋转环时，射线与环平面可能平行；改用视平面上的切向距离，避免无限交点。
        linearRotation=std::fabs(rotationAxis.Dot(_CameraForward))<.15f;
        normal=linearRotation?_CameraForward:rotationAxis;
    } else if(plane)normal=_Axes[first].Cross(_Axes[second]);
    else if(axis>=0) {
        // 约束平面包含所选轴，并尽量正对相机；投影到轴后得到稳定的一维位移。
        normal=_CameraForward-_Axes[axis]*_CameraForward.Dot(_Axes[axis]);
        if(!normal.TryNormalize(normal)){error="Selected axis points directly into the camera.";return false;}
    } else normal=_CameraForward;
    if(!normal.TryNormalize(normal)||!SceneGizmoDetail::RayPlane(ray,_Pivot,normal,start)) {
        error="The drag ray is parallel to its constraint plane.";return false;
    }
    Vector3 localPivot;
    if(_Settings.Tool!=SceneGizmoTool::Move&&_Settings.Pivot==SceneGizmoPivot::Center&&_Target.HasBoundsCenter) {
        Mat4 inverseWorld;
        if(!GameSproutRuntime::RuntimeMath::TryInverseAffine(_World,inverseWorld)) {
            error="Center manipulation requires a non-singular object transform.";return false;
        }
        localPivot=SceneGizmoDetail::Point(inverseWorld,_Pivot);
        if(!SceneGizmoDetail::Finite(localPivot)){error="The local center cannot be represented.";return false;}
    }
    Vector3 rotationStart,linearDirection;
    if(_Settings.Tool==SceneGizmoTool::Rotate) {
        if(linearRotation) {
            if(!rotationAxis.Cross(_CameraForward).TryNormalize(linearDirection)){error="Rotation tangent is undefined.";return false;}
        } else if(!(start-_Pivot).TryNormalize(rotationStart)) {error="The rotation pointer is at the pivot.";return false;}
    }
    _Active=handle;_Hovered=handle;_PlaneNormal=normal;_StartPlanePoint=start;_LocalPivot=localPivot;
    _StartPixel=pixel;
    _RotationAxis=rotationAxis;_RotationStart=rotationStart;_LinearRotation=linearRotation;_LinearRotationDirection=linearDirection;
    _PreviousAngle=0;_AngleTurns=0;_Dragging=true;return true;
}

bool SceneGizmo::SetCenteredPosition(SceneGizmoTransform& candidate,std::string& error) const {
    if(_Settings.Pivot!=SceneGizmoPivot::Center||!_Target.HasBoundsCenter)return true;
    SceneGizmoTransform linear=candidate;linear.Position={};
    try {candidate.Position=SceneGizmoDetail::Point(_InverseParent,_Pivot)-SceneGizmoDetail::Direction(SceneGizmoDetail::Compose(linear),_LocalPivot);}
    catch(...){error="Centered transform cannot be represented.";return false;}
    return SceneGizmoDetail::Finite(candidate.Position)||(error="Centered position overflows.",false);
}

bool SceneGizmo::ValidateCandidate(const SceneGizmoTransform& candidate,std::string& error) const {
    try {
        if(!GameSproutRuntime::RuntimeMath::IsFinite(_Target.ParentWorld*SceneGizmoDetail::Compose(candidate))) {
            error="Manipulation would overflow the world transform.";return false;
        }
    }catch(...){error="Manipulation produced invalid TRS.";return false;}
    return true;
}

bool SceneGizmo::Drag(Vector2 pixel,bool snap,SceneGizmoTransform& output,std::string& error) {
    error.clear();if(!_Dragging){error="No gizmo drag is active.";return false;}
    // 同一像素的射线/平面重复求交仍可能有舍入差；点击释放必须严格返回输入值。
    if(pixel.X==_StartPixel.X&&pixel.Y==_StartPixel.Y) {
        _PreviousAngle=0;output=_Target.Local;_DrawPivot=_Pivot;return true;
    }
    Ray ray;Vector3 point;
    if(!MakeRay(_View,pixel,ray)||!SceneGizmoDetail::RayPlane(ray,_Pivot,_PlaneNormal,point)) {
        error="The pointer ray does not intersect the drag plane.";return false;
    }
    const int axis=SceneGizmoDetail::AxisIndex(_Active);int first=0,second=1;
    const bool plane=SceneGizmoDetail::PlaneAxes(_Active,first,second);
    SceneGizmoTransform candidate=_Target.Local;Vector3 drawPivot=_Pivot;
    const auto unchanged=[&] {output=_Target.Local;_DrawPivot=_Pivot;return true;};
    if(_Settings.Tool==SceneGizmoTool::Move) {
        const Vector3 difference=point-_StartPlanePoint;Vector3 delta;
        if(axis>=0)delta=_Axes[axis]*SceneGizmoDetail::Snap(difference.Dot(_Axes[axis]),_Settings.TranslationSnap,snap);
        else if(plane)delta=_Axes[first]*SceneGizmoDetail::Snap(difference.Dot(_Axes[first]),_Settings.TranslationSnap,snap)+
            _Axes[second]*SceneGizmoDetail::Snap(difference.Dot(_Axes[second]),_Settings.TranslationSnap,snap);
        else {
            for(int component=0;component<3;++component)delta=delta+_Axes[component]*
                SceneGizmoDetail::Snap(difference.Dot(_Axes[component]),_Settings.TranslationSnap,snap);
        }
        if(delta.X==0&&delta.Y==0&&delta.Z==0)return unchanged();
        // 位置是世界空间位移；逆父矩阵必须作用于向量，不能直接加到 localPosition。
        candidate.Position=_Target.Local.Position+SceneGizmoDetail::Direction(_InverseParent,delta);
        drawPivot=_Pivot+delta;
    } else if(_Settings.Tool==SceneGizmoTool::Rotate) {
        double angle=0;
        if(_LinearRotation)angle=(point-_StartPlanePoint).Dot(_LinearRotationDirection)/_Length;
        else {
            Vector3 direction;
            if(!(point-_Pivot).TryNormalize(direction)){error="Rotation pointer is at the pivot.";return false;}
            const double raw=std::atan2(_RotationAxis.Dot(_RotationStart.Cross(direction)),_RotationStart.Dot(direction));
            const double delta=raw-_PreviousAngle;
            if(delta>GameSproutRuntime::Pi)_AngleTurns-=2.0*GameSproutRuntime::Pi;
            if(delta<-GameSproutRuntime::Pi)_AngleTurns+=2.0*GameSproutRuntime::Pi;
            _PreviousAngle=raw;angle=raw+_AngleTurns;
        }
        angle=SceneGizmoDetail::Snap(float(angle),GameSproutRuntime::DegreesToRadians(_Settings.RotationSnapDegrees),snap);
        if(angle==0)return unchanged();
        if(_Settings.Space==SceneGizmoSpace::Local&&axis>=0) {
            Vector3 localAxis;SceneGizmoDetail::SetComponent(localAxis,axis,1);
            candidate.Rotation=SceneGizmoDetail::Multiply(_Target.Local.Rotation,SceneGizmoDetail::AngleAxis(localAxis,angle));
            if(!candidate.Rotation.TryNormalize(candidate.Rotation)||!SetCenteredPosition(candidate,error))return false;
        } else {
            const Mat4 rotation=GameSproutRuntime::RuntimeMath::ComposeTRS({},SceneGizmoDetail::AngleAxis(_RotationAxis,angle),{1,1,1});
            const Mat4 local=_InverseParent*(SceneGizmoDetail::AroundPivot(rotation,_Pivot)*_World);
            if(!GameSproutRuntime::RuntimeMath::TryDecomposeTRS(local,candidate.Position,candidate.Rotation,candidate.Scale)) {
                error="父节点缩放使这次旋转产生剪切，请使用 Local 模式的轴旋转环。";return false;
            }
            SceneGizmoDetail::PreserveScaleSigns(candidate,_Target.Local);
        }
    } else {
        const Vector3 difference=point-_StartPlanePoint;Vector3 factors{1,1,1};
        if(axis>=0)SceneGizmoDetail::SetComponent(factors,axis,1+SceneGizmoDetail::Snap(difference.Dot(_Axes[axis])/_Length,_Settings.ScaleSnap,snap));
        else if(plane) {
            SceneGizmoDetail::SetComponent(factors,first,1+SceneGizmoDetail::Snap(difference.Dot(_Axes[first])/_Length,_Settings.ScaleSnap,snap));
            SceneGizmoDetail::SetComponent(factors,second,1+SceneGizmoDetail::Snap(difference.Dot(_Axes[second])/_Length,_Settings.ScaleSnap,snap));
        } else {
            Vector3 diagonal;(_CameraRight+_CameraUp).TryNormalize(diagonal);
            const float uniform=1+SceneGizmoDetail::Snap(difference.Dot(diagonal)/_Length,_Settings.ScaleSnap,snap);
            factors={uniform,uniform,uniform};
        }
        if(factors.X==1&&factors.Y==1&&factors.Z==1)return unchanged();
        if(!SceneGizmoDetail::Finite(factors)||std::fabs(factors.X)<1e-6f||std::fabs(factors.Y)<1e-6f||std::fabs(factors.Z)<1e-6f) {
            error="缩放不能恰好为零，请继续拖动或按 Esc 取消。";return false;
        }
        if(_Settings.Space==SceneGizmoSpace::Local||_Active==SceneGizmoHandle::Center) {
            candidate.Scale={candidate.Scale.X*factors.X,candidate.Scale.Y*factors.Y,candidate.Scale.Z*factors.Z};
            if(!SetCenteredPosition(candidate,error))return false;
        } else {
            const Mat4 local=_InverseParent*(SceneGizmoDetail::ScaleAlongAxes(_Axes,factors,_Pivot)*_World);
            if(!GameSproutRuntime::RuntimeMath::TryDecomposeTRS(local,candidate.Position,candidate.Rotation,candidate.Scale)) {
                error="当前旋转或父节点缩放使世界轴缩放产生剪切，请切换 Local 模式。";return false;
            }
            SceneGizmoDetail::PreserveScaleSigns(candidate,_Target.Local);
        }
    }
    if(!ValidateCandidate(candidate,error))return false;
    output=candidate;_DrawPivot=drawPivot;return true;
}

void SceneGizmo::EndDrag() {_Dragging=false;_Active=SceneGizmoHandle::None;_Hovered=SceneGizmoHandle::None;}
void SceneGizmo::CancelDrag() {EndDrag();_DrawPivot=_Pivot;}
