#include "SceneUIRectTool.h"
#include "SceneGizmoMath.h"
#include <array>
#include <numbers>

namespace {
using GameSproutRuntime::Color;
using GameSproutRuntime::UIDebugDrawList;
using GameSproutRuntime::UIScreenPreviewGeometry;
using GameSproutRuntime::Vector2;
using Handle = SceneUIRectTool::Handle;

constexpr std::array Corners{Handle::TopLeft,Handle::TopRight,Handle::BottomRight,Handle::BottomLeft};
constexpr std::array Sides{Handle::Top,Handle::Right,Handle::Bottom,Handle::Left};
constexpr Color XColor{1.0f,.29f,.31f,1}, YColor{.53f,.92f,.29f,1};
constexpr Color EdgeColor{.95f,.97f,1.0f,.95f}, CornerColor{.3f,.71f,1.0f,1};
constexpr Color SelectedColor{1.0f,.84f,.30f,1};
constexpr float MoveLength=54.0f;

float Dot(Vector2 a,Vector2 b) { return a.X*b.X+a.Y*b.Y; }
float Cross(Vector2 a,Vector2 b) { return a.X*b.Y-a.Y*b.X; }
Vector2 Unit(Vector2 vector) {
    const float length=std::hypot(vector.X,vector.Y);
    return length>1e-6f?vector*(1.0f/length):Vector2{};
}
bool Coordinates(Vector2 value,Vector2 right,Vector2 up,Vector2& result) {
    const float determinant=Cross(right,up);
    if(std::abs(determinant)<=1e-6f*std::sqrt(Dot(right,right)*Dot(up,up)))return false;
    result={Cross(value,up)/determinant,Cross(right,value)/determinant};
    return SceneGizmoDetail::Finite(result);
}
Vector2 Center(const UIScreenPreviewGeometry& geometry) { return (geometry.Corners[0]+geometry.Corners[2])*.5f; }
Vector2 ResizeAxes(Handle handle) {
    switch(handle) {
    case Handle::Left: return {-1,0};
    case Handle::Top: return {0,1};
    case Handle::Right: return {1,0};
    case Handle::Bottom: return {0,-1};
    case Handle::TopLeft: return {-1,1};
    case Handle::TopRight: return {1,1};
    case Handle::BottomRight: return {1,-1};
    case Handle::BottomLeft: return {-1,-1};
    default: return {};
    }
}
void Quad(UIDebugDrawList& draw,Vector2 a,Vector2 b,Vector2 c,Vector2 d,Color color) {
    draw.Triangles.push_back({a,b,c,color});
    draw.Triangles.push_back({a,c,d,color});
}
void Line(UIDebugDrawList& draw,Vector2 a,Vector2 b,float width,Color color) {
    const auto axis=Unit(b-a);
    const auto thickness=Vector2{-axis.Y,axis.X}*(width*.5f);
    Quad(draw,a-thickness,b-thickness,b+thickness,a+thickness,color);
}
void DotHandle(UIDebugDrawList& draw,Vector2 center,float radius,Color color) {
    constexpr int sides=12;
    constexpr float step=2*std::numbers::pi_v<float>/sides;
    Vector2 previous=center+Vector2{radius,0};
    for(int index=1;index<=sides;++index) {
        const auto next=center+Vector2{std::cos(index*step),std::sin(index*step)}*radius;
        draw.Triangles.push_back({center,previous,next,color});
        previous=next;
    }
}
void Arrow(UIDebugDrawList& draw,Vector2 start,Vector2 axis,float scale,Color color) {
    const auto base=start+axis*((MoveLength-11.0f)*scale);
    const auto tip=start+axis*(MoveLength*scale);
    const auto width=Vector2{-axis.Y,axis.X}*(4.5f*scale);
    Line(draw,start,base,2.0f*scale,color);
    draw.Triangles.push_back({tip,base+width,base-width,color});
}
}

void SceneUIRectTool::Configure(const UIScreenPreviewGeometry& geometry,float pixelsPerPoint,Mode mode,bool local,bool center) {
    _Geometry=geometry;
    _PixelsPerPoint=pixelsPerPoint;
    _Mode=mode;
    _Local=local;
    _Center=center;
    _Ready=!geometry.IsCanvasRoot;
}
void SceneUIRectTool::Clear() {
    _Ready=false;
    _Hover=Handle::None;
}
Vector2 SceneUIRectTool::MoveOrigin() const { return _Center?Center(_Geometry):_Geometry.Pivot; }
Vector2 SceneUIRectTool::MoveAxis(bool horizontal) const {
    if(_Local)return Unit(horizontal?_Geometry.Right:_Geometry.Up);
    return horizontal?Vector2{1,0}:Vector2{0,-1};
}
bool SceneUIRectTool::IsResize(Handle handle) { return handle>=Handle::Left&&handle<=Handle::BottomLeft; }

SceneUIRectTool::Handle SceneUIRectTool::Hit(Vector2 pixel) const {
    if(!_Ready||_Mode==Mode::Outline)return Handle::None;
    const float scale=_PixelsPerPoint;
    if(_Mode==Mode::Move) {
        const auto origin=MoveOrigin(),x=MoveAxis(true),y=MoveAxis(false);
        Vector2 relative;
        if(Coordinates(pixel-origin,x,y,relative)&&relative.X>=10*scale&&relative.X<=26*scale&&
            relative.Y>=10*scale&&relative.Y<=26*scale)return Handle::XY;
        const float xDistance=SceneGizmoDetail::SegmentDistance(pixel,origin+x*scale*5,origin+x*scale*MoveLength);
        const float yDistance=SceneGizmoDetail::SegmentDistance(pixel,origin+y*scale*5,origin+y*scale*MoveLength);
        if(std::min(xDistance,yDistance)<=6*scale)return xDistance<=yDistance?Handle::X:Handle::Y;
        if(SceneGizmoDetail::DistanceSquared(origin,pixel)<=25*scale*scale)return Handle::XY;
        return Handle::None;
    }
    const auto& corners=_Geometry.Corners;
    for(std::size_t index=0;index<corners.size();++index)
        if(SceneGizmoDetail::DistanceSquared(corners[index],pixel)<=36*scale*scale)return Corners[index];
    for(std::size_t index=0;index<corners.size();++index) {
        const auto axes=ResizeAxes(Corners[index]);
        const auto diagonal=Unit(Unit(_Geometry.Right)*axes.X+Unit(_Geometry.Up)*axes.Y);
        const auto hotSpot=corners[index]+diagonal*(15*scale);
        if(SceneGizmoDetail::DistanceSquared(hotSpot,pixel)<=64*scale*scale)return Handle::Rotate;
    }
    for(std::size_t index=0;index<corners.size();++index)
        if(SceneGizmoDetail::SegmentDistance(pixel,corners[index],corners[(index+1)%corners.size()])<=5*scale)
            return Sides[index];
    Vector2 local;
    if(Coordinates(pixel-corners[3],_Geometry.Right,_Geometry.Up,local)&&local.X>=0&&local.Y>=0&&
        local.X<=_Geometry.Size.X&&local.Y<=_Geometry.Size.Y)return Handle::Inside;
    return Handle::None;
}
bool SceneUIRectTool::Hover(Vector2 pixel) {
    const auto hit=Hit(pixel);
    if(hit==_Hover)return false;
    _Hover=hit;
    return true;
}
Qt::CursorShape SceneUIRectTool::Cursor() const {
    const auto handle=IsDragging()?_Active:_Hover;
    if(handle==Handle::None)return Qt::ArrowCursor;
    if(handle==Handle::Rotate)return Qt::CrossCursor;
    if(!IsResize(handle))return Qt::SizeAllCursor;
    const auto axes=ResizeAxes(handle);
    const auto direction=Unit(_Geometry.Right)*axes.X+Unit(_Geometry.Up)*axes.Y;
    const float x=std::abs(direction.X),y=std::abs(direction.Y);
    if(y<.4142f*x)return Qt::SizeHorCursor;
    if(x<.4142f*y)return Qt::SizeVerCursor;
    return direction.X*direction.Y>=0?Qt::SizeFDiagCursor:Qt::SizeBDiagCursor;
}
void SceneUIRectTool::BuildDraw(UIDebugDrawList& draw) const {
    if(!_Ready)return;
    const auto selected=IsDragging()?_Active:_Hover;
    const auto& corners=_Geometry.Corners;
    const float scale=_PixelsPerPoint;
    for(std::size_t index=0;index<corners.size();++index) {
        const auto a=corners[index],b=corners[(index+1)%corners.size()];
        Line(draw,a+Vector2{scale,scale},b+Vector2{scale,scale},2*scale,{0,0,0,.75f});
        Line(draw,a,b,scale,selected==Sides[index]?SelectedColor:EdgeColor);
    }
    if(_Mode==Mode::Rect) {
        for(std::size_t index=0;index<corners.size();++index)
            DotHandle(draw,corners[index],4*scale,selected==Corners[index]?SelectedColor:CornerColor);
        const auto center=_Center?Center(_Geometry):_Geometry.Pivot;
        DotHandle(draw,center,4*scale,CornerColor);
        DotHandle(draw,center,2*scale,selected==Handle::Rotate?SelectedColor:EdgeColor);
    }
    if(_Mode==Mode::Move) {
        const auto origin=MoveOrigin(),x=MoveAxis(true)*scale,y=MoveAxis(false)*scale;
        const std::array plane{origin+(x+y)*11,origin+x*24+y*11,origin+(x+y)*24,origin+x*11+y*24};
        const Color planeColor=selected==Handle::XY?SelectedColor:CornerColor;
        Quad(draw,plane[0],plane[1],plane[2],plane[3],{planeColor.R,planeColor.G,planeColor.B,.38f});
        for(std::size_t index=0;index<plane.size();++index)
            Line(draw,plane[index],plane[(index+1)%plane.size()],scale,planeColor);
        Arrow(draw,origin,MoveAxis(true),scale,selected==Handle::X?SelectedColor:XColor);
        Arrow(draw,origin,MoveAxis(false),scale,selected==Handle::Y?SelectedColor:YColor);
        DotHandle(draw,origin,3*scale,selected==Handle::XY?SelectedColor:EdgeColor);
    }
}

void SceneUIRectTool::Begin(Handle handle,Vector2 pixel,Vector2 pivot,const UIRectAuthoringValues& initial) {
    _Active=handle;
    _Start=_Geometry;
    _StartPixelsPerPoint=_PixelsPerPoint;
    _StartPixel=pixel;
    _Initial=_Latest=initial;
    _Pivot=pivot;
    _AxisX=MoveAxis(true);
    _AxisY=MoveAxis(false);
    _Moved=false;
    _PreviousAngle=_Rotation=0;
}
void SceneUIRectTool::End() {
    _Active=Handle::None;
    _Hover=Handle::None;
    _Moved=false;
}
bool SceneUIRectTool::Drag(Vector2 pixel,bool shift,bool alt,float moveSnap,float rotateSnap,UIRectAuthoringValues& result) {
    if(!IsDragging())return false;
    const auto delta=pixel-_StartPixel;
    const float threshold=_Active==Handle::Inside?3.0f*_StartPixelsPerPoint:0.0f;
    if(!_Moved&&std::hypot(delta.X,delta.Y)<=threshold)return false;
    result=_Initial;
    const bool valid=IsResize(_Active)?DragResize(delta,shift,alt,moveSnap,result):
        _Active==Handle::Rotate?DragRotate(pixel,shift?15.0f:rotateSnap,result):DragMove(delta,shift,moveSnap,result);
    if(!valid)return false;
    _Moved=true;
    _Latest=result;
    return true;
}
bool SceneUIRectTool::DragMove(Vector2 delta,bool shift,float snap,UIRectAuthoringValues& result) const {
    if(_Active==Handle::X||_Active==Handle::Y) {
        const auto axis=_Active==Handle::X?_AxisX:_AxisY;
        delta=axis*Dot(axis,delta);
    } else if(shift) {
        const auto x=_Active==Handle::Inside?Unit(_Start.Right):_AxisX;
        const auto y=_Active==Handle::Inside?Unit(_Start.Up):_AxisY;
        const float xDistance=Dot(x,delta),yDistance=Dot(y,delta);
        delta=std::abs(xDistance)>=std::abs(yDistance)?x*xDistance:y*yDistance;
    }
    Vector2 position;
    if(!Coordinates(delta,_Start.ParentRight,_Start.ParentUp,position))return false;
    if(snap>0) {
        // 对约束后的位移整体量化，吸附不会把对象带离拖动轴。
        const float magnitude=std::hypot(position.X,position.Y);
        if(magnitude>0)position=position*(SceneGizmoDetail::Snap(magnitude,snap,true)/magnitude);
    }
    result.AnchoredPosition=_Initial.AnchoredPosition+position;
    return true;
}
bool SceneUIRectTool::DragResize(Vector2 delta,bool shift,bool alt,float snap,UIRectAuthoringValues& result) const {
    Vector2 local;
    if(!Coordinates(delta,_Start.Right,_Start.Up,local))return false;
    const auto axes=ResizeAxes(_Active);
    const auto base=alt?(_Center?Vector2{.5f,.5f}:_Pivot):
        Vector2{.5f-axes.X*.5f,.5f-axes.Y*.5f};
    const auto edge=Vector2{.5f+axes.X*.5f,.5f+axes.Y*.5f};
    const float xSpan=edge.X-base.X,ySpan=edge.Y-base.Y;
    Vector2 change{axes.X!=0&&std::abs(xSpan)>1e-6f?local.X/xSpan:0,
                   axes.Y!=0&&std::abs(ySpan)>1e-6f?local.Y/ySpan:0};
    if(snap>0) {
        change.X=SceneGizmoDetail::Snap(change.X,snap,true);
        change.Y=SceneGizmoDetail::Snap(change.Y,snap,true);
    }
    const auto size=_Start.Size;
    if(shift&&size.X>0&&size.Y>0) {
        const float x=change.X/size.X,y=change.Y/size.Y;
        const float proportion=std::max(-1.0f,std::abs(x)>=std::abs(y)?x:y);
        change=size*proportion;
    }
    change={std::max(-size.X,change.X),std::max(-size.Y,change.Y)};
    const auto offset=_Start.Right*((_Pivot.X-base.X)*change.X)+_Start.Up*((_Pivot.Y-base.Y)*change.Y);
    Vector2 position;
    if(!Coordinates(offset,_Start.ParentRight,_Start.ParentUp,position))return false;
    result.AnchoredPosition=_Initial.AnchoredPosition+position;
    result.SizeDelta=_Initial.SizeDelta+change;
    return true;
}
bool SceneUIRectTool::DragRotate(Vector2 pixel,float snap,UIRectAuthoringValues& result) {
    const auto center=_Center?Center(_Start):_Start.Pivot;
    Vector2 initial,current;
    if(!Coordinates(_StartPixel-center,_Start.ParentRight,_Start.ParentUp,initial)||
       !Coordinates(pixel-center,_Start.ParentRight,_Start.ParentUp,current)||Dot(current,current)<1e-8f)return false;
    const float angle=std::atan2(Cross(initial,current),Dot(initial,current));
    _Rotation+=std::remainder(angle-_PreviousAngle,2*std::numbers::pi_v<float>);
    _PreviousAngle=angle;
    const float rotation=SceneGizmoDetail::Snap(_Rotation,GameSproutRuntime::DegreesToRadians(snap),snap>0);
    const auto delta=SceneGizmoDetail::AngleAxis({0,0,1},rotation);
    if(!SceneGizmoDetail::Multiply(delta,_Initial.LocalRotation).TryNormalize(result.LocalRotation))return false;
    if(_Center) {
        Vector2 fromCenter;
        if(!Coordinates(_Start.Pivot-center,_Start.ParentRight,_Start.ParentUp,fromCenter))return false;
        const float sine=std::sin(rotation),cosine=std::cos(rotation);
        const Vector2 rotated{cosine*fromCenter.X-sine*fromCenter.Y,sine*fromCenter.X+cosine*fromCenter.Y};
        result.AnchoredPosition=_Initial.AnchoredPosition+rotated-fromCenter;
    }
    return true;
}
