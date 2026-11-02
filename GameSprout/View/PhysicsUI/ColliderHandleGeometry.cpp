#include "ColliderHandleGeometry.h"
#include "Math/Projection.h"
#include <algorithm>
#include <cmath>

namespace {
float Component(GameSproutRuntime::Vector3 value,unsigned axis){return axis==0?value.X:axis==1?value.Y:value.Z;}
void Set(GameSproutRuntime::Vector3& value,unsigned axis,float item){if(axis==0)value.X=item;else if(axis==1)value.Y=item;else value.Z=item;}
GameSproutRuntime::Vector3 Point(const GameSproutRuntime::Mat4& matrix,GameSproutRuntime::Vector3 value) {
    const auto result=matrix*GameSproutRuntime::Vector4{value.X,value.Y,value.Z,1};return {result.X,result.Y,result.Z};
}
GameSproutRuntime::Vector3 Direction(const GameSproutRuntime::Mat4& matrix,GameSproutRuntime::Vector3 value) {
    const auto result=matrix*GameSproutRuntime::Vector4{value.X,value.Y,value.Z,0};return {result.X,result.Y,result.Z};
}
bool Projection(const SceneToolContext& view,GameSproutRuntime::Mat4& output) {
    if(!view.Frame.Width||!view.Frame.Height)return false;
    const auto& settings=view.Frame.Camera.Projection;const float aspect=float(view.Frame.Width)/view.Frame.Height;
    return settings.Projection==GameSproutRuntime::CameraProjection::Perspective?
        GameSproutRuntime::TryMakePerspective(settings.VerticalFovRadians,aspect,settings.NearPlane,settings.FarPlane,output):
        GameSproutRuntime::TryMakeOrthographic(settings.OrthographicHeight,aspect,settings.NearPlane,settings.FarPlane,output);
}
bool PlanePoint(const GameSproutRuntime::Ray& ray,GameSproutRuntime::Vector3 origin,GameSproutRuntime::Vector3 normal,GameSproutRuntime::Vector3& output) {
    const float denominator=ray.Direction.Dot(normal);if(std::fabs(denominator)<1e-5f)return false;
    const float t=(origin-ray.Origin).Dot(normal)/denominator;if(!std::isfinite(t))return false;
    output=ray.GetPoint(t);return GameSproutRuntime::RuntimeMath::IsFinite(output);
}
void Line(GameSproutRuntime::DebugDrawList& draw,GameSproutRuntime::Vector3 a,GameSproutRuntime::Vector3 b,GameSproutRuntime::Color tint) {
    draw.Lines.push_back({a,b,tint,false});
}
}
bool ColliderHandleGeometry::Project(const SceneToolContext& context,GameSproutRuntime::Vector3 world,GameSproutRuntime::Vector2& output) {
    GameSproutRuntime::Mat4 projection;if(!Projection(context,projection))return false;
    const auto clip=projection*(context.Frame.Camera.View*GameSproutRuntime::Vector4{world.X,world.Y,world.Z,1});
    if(!std::isfinite(clip.W)||clip.W<1e-6f||clip.Z<0||clip.Z>clip.W)return false;
    output={(clip.X/clip.W+1)*.5f*context.Frame.Width,(1-clip.Y/clip.W)*.5f*context.Frame.Height};
    return std::isfinite(output.X)&&std::isfinite(output.Y);
}
bool ColliderHandleGeometry::MakeRay(const SceneToolContext& context,GameSproutRuntime::Vector2 pixel,GameSproutRuntime::Ray& output) {
    GameSproutRuntime::Mat4 projection,inverse;
    if(!Projection(context,projection)||!GameSproutRuntime::RuntimeMath::TryInverseAffine(context.Frame.Camera.View,inverse))return false;
    const float x=2*pixel.X/context.Frame.Width-1,y=1-2*pixel.Y/context.Frame.Height;
    if(context.Frame.Camera.Projection.Projection==GameSproutRuntime::CameraProjection::Perspective) {
        output.Origin=Point(inverse,{});return Direction(inverse,{x/projection(0,0),y/projection(1,1),-1}).TryNormalize(output.Direction);
    }
    output.Origin=Point(inverse,{x/projection(0,0),y/projection(1,1),-context.Frame.Camera.Projection.NearPlane});
    return Direction(inverse,{0,0,-1}).TryNormalize(output.Direction);
}
bool ColliderHandleGeometry::Update(const ColliderEditState& state,const GameSproutRuntime::Mat4& objectWorld,
    const SceneToolContext& context,std::string& error) {
    error.clear();_Valid=false;
    if(state.Kind==ColliderShapeKind::Mesh){error="Mesh Collider 的形状由网格决定，不提供基础形状尺寸手柄。";return false;}
    GameSproutRuntime::RuntimePhysicsMath::ColliderFrame frame;
    const auto rotation=state.Kind==ColliderShapeKind::Sphere?GameSproutRuntime::Quaternion{}:state.Settings.Rotation;
    if(!GameSproutRuntime::RuntimePhysicsMath::TryGetColliderFrame(objectWorld*GameSproutRuntime::RuntimeMath::ComposeTRS(state.Settings.Center,rotation,{1,1,1}),frame)
        ||!GameSproutRuntime::RuntimeMath::TryInverseAffine(objectWorld,_InverseWorld)) {
        error="碰撞体编辑需要非零缩放且不含剪切的变换。";return false;
    }
    _State=state;_World=objectWorld;_View=context;_Frame=frame;
    const auto axesRotation=GameSproutRuntime::RuntimeMath::ComposeTRS({},frame.Rotation,{1,1,1});
    _Axes={Direction(axesRotation,{1,0,0}),Direction(axesRotation,{0,1,0}),Direction(axesRotation,{0,0,1})};
    const auto scale=GameSproutRuntime::RuntimePhysicsMath::AbsoluteScale(frame.Scale);
    if(state.Kind==ColliderShapeKind::Box)_Extents={state.Box.Size.X*scale.X*.5f,state.Box.Size.Y*scale.Y*.5f,state.Box.Size.Z*scale.Z*.5f};
    else if(state.Kind==ColliderShapeKind::Sphere) {
        const auto radius=state.Sphere.Radius*GameSproutRuntime::RuntimePhysicsMath::SphereScale(scale);_Extents={radius,radius,radius};
    } else {
        const auto radius=state.Capsule.Radius*GameSproutRuntime::RuntimePhysicsMath::CapsuleRadiusScale(scale,state.Capsule.Axis);
        const auto height=std::max(2*radius,state.Capsule.Height*GameSproutRuntime::RuntimePhysicsMath::CapsuleHeightScale(scale,state.Capsule.Axis));
        _Extents={radius,radius,radius};Set(_Extents,static_cast<unsigned>(state.Capsule.Axis),height*.5f);
    }
    for(unsigned axis=0;axis<3;++axis)for(unsigned sign=0;sign<2;++sign)
        _Handles[axis*2+sign]=frame.Position+_Axes[axis]*(Component(_Extents,axis)*(sign?1:-1));
    const auto cameraPoint=context.Frame.Camera.View*GameSproutRuntime::Vector4{frame.Position.X,frame.Position.Y,frame.Position.Z,1};
    const auto& projection=context.Frame.Camera.Projection;
    const float worldHeight=projection.Projection==GameSproutRuntime::CameraProjection::Perspective?
        2*std::max(.001f,-cameraPoint.Z)*std::tan(projection.VerticalFovRadians*.5f):projection.OrthographicHeight;
    _WorldPerPixel=worldHeight/std::max(1u,context.Frame.Height);_HandleSize=std::max(.00001f,_WorldPerPixel*4.5f*std::max(1.f,context.PixelRatio));
    _Valid=GameSproutRuntime::RuntimeMath::IsFinite(_Extents)&&std::isfinite(_HandleSize);return _Valid;
}
int ColliderHandleGeometry::Hover(GameSproutRuntime::Vector2 pixel) {
    if(_Dragging)return _Active;
    _Hovered=-1;if(!_Valid)return -1;
    float best=100*std::max(1.f,_View.PixelRatio)*std::max(1.f,_View.PixelRatio);float bestDepth=1e30f;
    for(unsigned index=0;index<6;++index) {
        GameSproutRuntime::Vector2 projected;if(!Project(_View,_Handles[index],projected))continue;
        const auto difference=projected-pixel;const float distance=difference.X*difference.X+difference.Y*difference.Y;
        const auto camera=_View.Frame.Camera.View*GameSproutRuntime::Vector4{_Handles[index].X,_Handles[index].Y,_Handles[index].Z,1};
        if(distance<best-.01f||(std::fabs(distance-best)<.01f&&-camera.Z<bestDepth)){best=distance;bestDepth=-camera.Z;_Hovered=int(index);}
    }
    return _Hovered;
}
bool ColliderHandleGeometry::Begin(GameSproutRuntime::Vector2 pixel,std::string& error) {
    error.clear();const auto handle=Hover(pixel);if(handle<0){error="鼠标未命中碰撞体手柄。";return false;}
    _Start=_State;_StartView=_View;_StartFrame=_Frame;_StartInverseWorld=_InverseWorld;_StartExtents=_Extents;_StartPixel=pixel;
    _Active=handle;_DragAxis=_Axes[unsigned(handle)/2];
    GameSproutRuntime::Mat4 inverse;GameSproutRuntime::RuntimeMath::TryInverseAffine(_View.Frame.Camera.View,inverse);
    const auto cameraForward=Direction(inverse,{0,0,-1});
    _LinearDrag=!(cameraForward-_DragAxis*cameraForward.Dot(_DragAxis)).TryNormalize(_DragPlaneNormal);
    GameSproutRuntime::Ray ray;
    if(!_LinearDrag&&(!MakeRay(_View,pixel,ray)||!PlanePoint(ray,_Handles[handle],_DragPlaneNormal,_StartPlanePoint)))_LinearDrag=true;
    _Dragging=true;return true;
}
bool ColliderHandleGeometry::Drag(const SceneToolPointer& pointer,ColliderEditState& output,std::string& error) const {
    error.clear();if(!_Dragging){error="没有活动的碰撞体手柄拖拽。";return false;}
    if(pointer.Pixel.X==_StartPixel.X&&pointer.Pixel.Y==_StartPixel.Y){output=_Start;return true;}
    float delta=0;
    if(!_LinearDrag) {
        GameSproutRuntime::Ray ray;GameSproutRuntime::Vector3 point;
        if(!MakeRay(_StartView,pointer.Pixel,ray)||!PlanePoint(ray,_StartPlanePoint,_DragPlaneNormal,point)){error="鼠标射线未与碰撞体编辑平面相交。";return false;}
        delta=(point-_StartPlanePoint).Dot(_DragAxis);
    }else delta=(_StartPixel.Y-pointer.Pixel.Y)*_WorldPerPixel;
    if(pointer.Snap&&std::isfinite(_StartView.SnapDistance)&&_StartView.SnapDistance>0)delta=std::round(delta/_StartView.SnapDistance)*_StartView.SnapDistance;
    if(!std::isfinite(delta)){error="碰撞体拖拽结果不是有限数值。";return false;}
    const unsigned axis=unsigned(_Active)/2;const float sign=_Active%2?1.f:-1.f;
    const float oldDiameter=2*Component(_StartExtents,axis);
    const float diameter=std::max(.0001f,oldDiameter+sign*delta*(pointer.Alt?2.f:1.f));
    const float actualDelta=(diameter-oldDiameter)*sign;
    output=_Start;
    const auto center=_StartFrame.Position+_DragAxis*(pointer.Alt?0.f:actualDelta*.5f);
    output.Settings.Center=Point(_StartInverseWorld,center);
    const auto scale=GameSproutRuntime::RuntimePhysicsMath::AbsoluteScale(_StartFrame.Scale);
    if(output.Kind==ColliderShapeKind::Box) {
        if(pointer.Shift&&oldDiameter>0)output.Box.Size=output.Box.Size*(diameter/oldDiameter);
        else Set(output.Box.Size,axis,diameter/Component(scale,axis));
    }else if(output.Kind==ColliderShapeKind::Sphere)
        output.Sphere.Radius=diameter*.5f/GameSproutRuntime::RuntimePhysicsMath::SphereScale(scale);
    else {
        const auto heightScale=GameSproutRuntime::RuntimePhysicsMath::CapsuleHeightScale(scale,output.Capsule.Axis);
        const auto radiusScale=GameSproutRuntime::RuntimePhysicsMath::CapsuleRadiusScale(scale,output.Capsule.Axis);
        if(pointer.Shift&&oldDiameter>0) {
            const auto factor=diameter/oldDiameter;
            output.Capsule.Radius*=factor;output.Capsule.Height*=factor;
        } else if(axis==static_cast<unsigned>(output.Capsule.Axis)) {
            const auto minimum=2*output.Capsule.Radius*radiusScale;
            const auto height=std::max(diameter,minimum);
            output.Capsule.Height=std::max(2*output.Capsule.Radius,height/heightScale);
            const float moved=(height-oldDiameter)*sign;
            output.Settings.Center=Point(_StartInverseWorld,_StartFrame.Position+_DragAxis*(pointer.Alt?0.f:moved*.5f));
        } else {
            output.Capsule.Radius=diameter*.5f/radiusScale;
            output.Capsule.Height=std::max(output.Capsule.Height,2*output.Capsule.Radius);
        }
    }
    return true;
}
void ColliderHandleGeometry::Draw(GameSproutRuntime::DebugDrawList& draw) const {
    if(!_Valid)return;
    const auto normal=_State.Settings.Enabled?GameSproutRuntime::Color{.2f,1,.25f,1}:GameSproutRuntime::Color{.6f,.65f,.7f,1};
    const auto world=[&](GameSproutRuntime::Vector3 local){return _Frame.Position+_Axes[0]*local.X+_Axes[1]*local.Y+_Axes[2]*local.Z;};
    const auto line=[&](GameSproutRuntime::Vector3 a,GameSproutRuntime::Vector3 b){Line(draw,world(a),world(b),normal);};
    // The active tool target can differ from Hierarchy selection when Inspector
    // is locked, so its exact physical outline must not depend on selection gizmos.
    if(_State.Kind==ColliderShapeKind::Box) {
        std::array<GameSproutRuntime::Vector3,8> corners;
        for(unsigned corner=0;corner<8;++corner)corners[corner]={(corner&1)?_Extents.X:-_Extents.X,(corner&2)?_Extents.Y:-_Extents.Y,(corner&4)?_Extents.Z:-_Extents.Z};
        for(unsigned corner=0;corner<8;++corner)for(unsigned axis=0;axis<3;++axis)if(!(corner&(1u<<axis)))line(corners[corner],corners[corner|(1u<<axis)]);
    } else if(_State.Kind==ColliderShapeKind::Sphere) {
        constexpr float tau=6.2831853071795864769f;
        for(unsigned plane=0;plane<3;++plane)for(unsigned step=0;step<32;++step) {
            GameSproutRuntime::Vector3 a,b;const unsigned first=(plane+1)%3,second=(plane+2)%3;
            Set(a,first,_Extents.X*std::cos(tau*step/32));Set(a,second,_Extents.X*std::sin(tau*step/32));
            Set(b,first,_Extents.X*std::cos(tau*(step+1)/32));Set(b,second,_Extents.X*std::sin(tau*(step+1)/32));line(a,b);
        }
    } else {
        constexpr float tau=6.2831853071795864769f;
        const unsigned height=static_cast<unsigned>(_State.Capsule.Axis),first=(height+1)%3,second=(height+2)%3;
        const float radius=Component(_Extents,first),halfCylinder=std::max(0.f,Component(_Extents,height)-radius);
        const auto point=[&](float x,float y,float z){GameSproutRuntime::Vector3 value;Set(value,first,x);Set(value,height,y);Set(value,second,z);return value;};
        for(float side:{-1.f,1.f})for(unsigned step=0;step<32;++step)
            line(point(radius*std::cos(tau*step/32),side*halfCylinder,radius*std::sin(tau*step/32)),
                point(radius*std::cos(tau*(step+1)/32),side*halfCylinder,radius*std::sin(tau*(step+1)/32)));
        for(unsigned meridian=0;meridian<4;++meridian) {
            const float x=std::cos(tau*meridian/4),z=std::sin(tau*meridian/4);
            line(point(x*radius,halfCylinder,z*radius),point(x*radius,-halfCylinder,z*radius));
            for(float side:{-1.f,1.f})for(unsigned step=0;step<8;++step)
                line(point(x*radius*std::cos(tau*step/32),side*(halfCylinder+radius*std::sin(tau*step/32)),z*radius*std::cos(tau*step/32)),
                    point(x*radius*std::cos(tau*(step+1)/32),side*(halfCylinder+radius*std::sin(tau*(step+1)/32)),z*radius*std::cos(tau*(step+1)/32)));
        }
    }
    for(unsigned handle=0;handle<6;++handle) {
        const auto tint=ActiveHandle()==int(handle)?GameSproutRuntime::Color{1,.7f,.12f,1}:normal;
        std::array<GameSproutRuntime::Vector3,8> corners;
        for(unsigned corner=0;corner<8;++corner)corners[corner]=_Handles[handle]+_Axes[0]*((corner&1)?_HandleSize:-_HandleSize)
            +_Axes[1]*((corner&2)?_HandleSize:-_HandleSize)+_Axes[2]*((corner&4)?_HandleSize:-_HandleSize);
        for(unsigned corner=0;corner<8;++corner)for(unsigned axis=0;axis<3;++axis)if(!(corner&(1u<<axis)))Line(draw,corners[corner],corners[corner|(1u<<axis)],tint);
        constexpr unsigned faces[][4]={{0,1,3,2},{4,6,7,5},{0,4,5,1},{2,3,7,6},{0,2,6,4},{1,5,7,3}};
        for(const auto& face:faces){draw.Triangles.push_back({corners[face[0]],corners[face[1]],corners[face[2]],tint,false});draw.Triangles.push_back({corners[face[0]],corners[face[2]],corners[face[3]],tint,false});}
    }
}
