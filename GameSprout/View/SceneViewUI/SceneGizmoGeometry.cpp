#include "SceneGizmo.h"
#include "SceneGizmoMath.h"

using GameSproutRuntime::Vector2;
using GameSproutRuntime::Vector3;
using GameSproutRuntime::Color;
using GameSproutRuntime::DebugDrawList;

namespace {
Color AxisColor(int axis) {
    return axis==0?Color{.96f,.25f,.23f,1}:axis==1?Color{.35f,.91f,.35f,1}:Color{.25f,.55f,1,1};
}
Color SelectedColor() {return {1,.86f,.20f,1};}
void Line(DebugDrawList& output,Vector3 first,Vector3 second,Color color) {
    if(SceneGizmoDetail::Finite(first)&&SceneGizmoDetail::Finite(second))output.Lines.push_back({first,second,color,false});
}
void Triangle(DebugDrawList& output,Vector3 a,Vector3 b,Vector3 c,Color color) {
    if(SceneGizmoDetail::Finite(a)&&SceneGizmoDetail::Finite(b)&&SceneGizmoDetail::Finite(c))output.Triangles.push_back({a,b,c,color,false});
}
void Quad(DebugDrawList& output,const Vector3 (&points)[4],Color color) {
    Color fill=color;fill.A=.20f;
    Triangle(output,points[0],points[1],points[2],fill);Triangle(output,points[0],points[2],points[3],fill);
    for(int corner=0;corner<4;++corner)Line(output,points[corner],points[(corner+1)%4],color);
}
void Cube(DebugDrawList& output,Vector3 center,const Vector3 (&axes)[3],float radius,Color color) {
    Vector3 corners[8];
    for(int index=0;index<8;++index)corners[index]=center+axes[0]*((index&1)?radius:-radius)+
        axes[1]*((index&2)?radius:-radius)+axes[2]*((index&4)?radius:-radius);
    for(int index=0;index<8;++index)for(int axis=0;axis<3;++axis)if(!(index&(1<<axis)))Line(output,corners[index],corners[index|(1<<axis)],color);
    const int faces[6][4]={{0,1,3,2},{4,6,7,5},{0,4,5,1},{2,3,7,6},{0,2,6,4},{1,5,7,3}};
    for(const auto& face:faces) {
        Triangle(output,corners[face[0]],corners[face[1]],corners[face[2]],color);
        Triangle(output,corners[face[0]],corners[face[2]],corners[face[3]],color);
    }
}
bool QuadContains(const SceneGizmoView& view,const Vector3 (&points)[4],Vector2 pixel) {
    Vector2 projected[4];for(int index=0;index<4;++index)if(!SceneGizmo::Project(view,points[index],projected[index]))return false;
    double area=0;for(int index=0;index<4;++index) {
        const auto a=projected[index],b=projected[(index+1)%4];area+=double(a.X)*b.Y-double(a.Y)*b.X;
    }
    if(std::fabs(area)<18)return false; // 从侧面看的平面不抢占轴句柄。
    bool positive=false,negative=false;
    for(int index=0;index<4;++index) {
        const auto edge=projected[(index+1)%4]-projected[index],difference=pixel-projected[index];
        const float cross=edge.X*difference.Y-edge.Y*difference.X;
        positive|=cross>.01f;negative|=cross<-.01f;
    }
    return !(positive&&negative);
}
}

SceneGizmoHandle SceneGizmo::HitTest(Vector2 pixel) const {
    if(!_Valid||!SceneGizmoDetail::Finite(pixel))return SceneGizmoHandle::None;
    Vector2 pivot;if(!Project(_View,_Pivot,pivot))return SceneGizmoHandle::None;
    const SceneGizmoHandle handles[]={SceneGizmoHandle::X,SceneGizmoHandle::Y,SceneGizmoHandle::Z};
    SceneGizmoHandle result=SceneGizmoHandle::None;float best=_Settings.HitRadiusPixels;
    if(_Settings.Tool==SceneGizmoTool::Rotate) {
        // 用与绘制相同的分段圆计算屏幕距离，不用世界空间固定半径导致远近命中范围变化。
        for(int ring=0;ring<4;++ring) {
            const Vector3 first=ring<3?_Axes[(ring+1)%3]:_CameraRight;
            const Vector3 second=ring<3?_Axes[(ring+2)%3]:_CameraUp;
            const float radius=_Length*(ring<3?1.0f:1.13f);
            for(int segment=0;segment<96;++segment) {
                const double a=2.0*GameSproutRuntime::Pi*segment/96,b=2.0*GameSproutRuntime::Pi*(segment+1)/96;
                Vector2 screenA,screenB;
                if(!Project(_View,_Pivot+(first*float(std::cos(a))+second*float(std::sin(a)))*radius,screenA)||
                    !Project(_View,_Pivot+(first*float(std::cos(b))+second*float(std::sin(b)))*radius,screenB))continue;
                const float distance=SceneGizmoDetail::SegmentDistance(pixel,screenA,screenB);
                if(distance<best) {best=distance;result=ring<3?handles[ring]:SceneGizmoHandle::Center;}
            }
        }
        return result;
    }
    if(SceneGizmoDetail::DistanceSquared(pixel,pivot)<=_Settings.HitRadiusPixels*_Settings.HitRadiusPixels)
        return SceneGizmoHandle::Center;
    for(int axis=0;axis<3;++axis) {
        Vector2 begin,end;
        if(!Project(_View,_Pivot+_Axes[axis]*(_Length*.14f),begin)||!Project(_View,_Pivot+_Axes[axis]*_Length,end)||
            SceneGizmoDetail::DistanceSquared(begin,end)<144)continue;
        const float distance=SceneGizmoDetail::SegmentDistance(pixel,begin,end);
        if(distance<best){best=distance;result=handles[axis];}
    }
    if(result!=SceneGizmoHandle::None)return result;
    const SceneGizmoHandle planes[]={SceneGizmoHandle::XY,SceneGizmoHandle::YZ,SceneGizmoHandle::ZX};
    for(const auto plane:planes) {
        int first=0,second=1;SceneGizmoDetail::PlaneAxes(plane,first,second);Vector3 corners[4];
        SceneGizmoDetail::PlaneQuad(_Pivot,_Axes,first,second,_Length,corners);
        if(QuadContains(_View,corners,pixel))return plane;
    }
    return SceneGizmoHandle::None;
}

void SceneGizmo::BuildDrawList(DebugDrawList& output) const {
    if(!_Valid)return;
    const auto active=ActiveHandle();
    const SceneGizmoHandle handles[]={SceneGizmoHandle::X,SceneGizmoHandle::Y,SceneGizmoHandle::Z};
    if(_Settings.Tool==SceneGizmoTool::Rotate) {
        for(int ring=0;ring<4;++ring) {
            const Vector3 first=ring<3?_Axes[(ring+1)%3]:_CameraRight;
            const Vector3 second=ring<3?_Axes[(ring+2)%3]:_CameraUp;
            const SceneGizmoHandle handle=ring<3?handles[ring]:SceneGizmoHandle::Center;
            Color color=handle==active?SelectedColor():ring<3?AxisColor(ring):Color{.88f,.89f,.92f,1};
            const float radius=_Length*(ring<3?1.0f:1.13f);
            for(int segment=0;segment<96;++segment) {
                const double a=2.0*GameSproutRuntime::Pi*segment/96,b=2.0*GameSproutRuntime::Pi*(segment+1)/96;
                Line(output,_DrawPivot+(first*float(std::cos(a))+second*float(std::sin(a)))*radius,
                    _DrawPivot+(first*float(std::cos(b))+second*float(std::sin(b)))*radius,color);
            }
        }
        return;
    }
    const SceneGizmoHandle planes[]={SceneGizmoHandle::XY,SceneGizmoHandle::YZ,SceneGizmoHandle::ZX};
    for(const auto plane:planes) {
        int first=0,second=1;SceneGizmoDetail::PlaneAxes(plane,first,second);Vector3 corners[4];
        SceneGizmoDetail::PlaneQuad(_DrawPivot,_Axes,first,second,_Length,corners);
        Quad(output,corners,plane==active?SelectedColor():AxisColor(3-first-second));
    }
    for(int axis=0;axis<3;++axis) {
        const Color color=handles[axis]==active?SelectedColor():AxisColor(axis);
        const Vector3 tip=_DrawPivot+_Axes[axis]*_Length;
        if(_Settings.Tool==SceneGizmoTool::Scale) {
            Line(output,_DrawPivot,tip,color);Cube(output,tip,_Axes,_Length*.055f,color);
        } else {
            const Vector3 base=_DrawPivot+_Axes[axis]*(_Length*.79f);
            Line(output,_DrawPivot,base,color);
            const Vector3 first=_Axes[(axis+1)%3],second=_Axes[(axis+2)%3];
            for(int segment=0;segment<10;++segment) {
                const double a=2.0*GameSproutRuntime::Pi*segment/10,b=2.0*GameSproutRuntime::Pi*(segment+1)/10;
                const Vector3 aPoint=base+(first*float(std::cos(a))+second*float(std::sin(a)))*(_Length*.065f);
                const Vector3 bPoint=base+(first*float(std::cos(b))+second*float(std::sin(b)))*(_Length*.065f);
                Triangle(output,aPoint,bPoint,tip,color);Triangle(output,base,bPoint,aPoint,color);
            }
        }
    }
    const Color center=active==SceneGizmoHandle::Center?SelectedColor():Color{.90f,.91f,.94f,1};
    if(_Settings.Tool==SceneGizmoTool::Scale)Cube(output,_DrawPivot,_Axes,_Length*.055f,center);
    else {
        const float radius=_Length*.065f;
        const Vector3 corners[]={_DrawPivot+(_CameraRight+_CameraUp)*radius,_DrawPivot+(-_CameraRight+_CameraUp)*radius,
            _DrawPivot+(-_CameraRight-_CameraUp)*radius,_DrawPivot+(_CameraRight-_CameraUp)*radius};
        Quad(output,corners,center);
    }
}
