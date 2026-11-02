#pragma once
#include "IPhysicsAuthoring.h"
#include "ISceneTool.h"
#include "RuntimeColliderMath.h"
#include <array>

// Pure handle geometry/input math. No nodes, widgets, modules or GPU ownership.
class ColliderHandleGeometry final {
public:
    bool Update(const ColliderEditState&,const GameSproutRuntime::Mat4& objectWorld,const SceneToolContext&,std::string& error);
    int Hover(GameSproutRuntime::Vector2 pixel);
    bool Begin(GameSproutRuntime::Vector2 pixel,std::string& error);
    bool Drag(const SceneToolPointer&,ColliderEditState&,std::string& error) const;
    void End() {_Dragging=false;_Active=-1;}
    bool IsDragging() const {return _Dragging;}
    int ActiveHandle() const {return _Dragging?_Active:_Hovered;}
    GameSproutRuntime::Vector3 HandlePosition(unsigned index) const {return index<6?_Handles[index]:GameSproutRuntime::Vector3{};}
    void Draw(GameSproutRuntime::DebugDrawList&) const;
    static bool Project(const SceneToolContext&,GameSproutRuntime::Vector3,GameSproutRuntime::Vector2&);
    static bool MakeRay(const SceneToolContext&,GameSproutRuntime::Vector2,GameSproutRuntime::Ray&);
private:
    ColliderEditState _State,_Start;
    SceneToolContext _View,_StartView;
    GameSproutRuntime::Mat4 _World,_InverseWorld,_StartInverseWorld;
    GameSproutRuntime::RuntimePhysicsMath::ColliderFrame _Frame,_StartFrame;
    std::array<GameSproutRuntime::Vector3,3> _Axes;
    std::array<GameSproutRuntime::Vector3,6> _Handles;
    GameSproutRuntime::Vector3 _Extents,_StartExtents,_DragAxis,_DragPlaneNormal,_StartPlanePoint;
    GameSproutRuntime::Vector2 _StartPixel;
    bool _Valid=false,_Dragging=false,_LinearDrag=false;
    float _HandleSize=0.05f,_WorldPerPixel=.01f;
    int _Hovered=-1,_Active=-1;
};
