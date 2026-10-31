#pragma once

#include "RuntimeRenderApi.h"
#include <cstdint>
#include <string>

enum class SceneGizmoTool {Move,Rotate,Scale};
enum class SceneGizmoSpace {World,Local};
enum class SceneGizmoPivot {Pivot,Center};
enum class SceneGizmoHandle {None,X,Y,Z,XY,YZ,ZX,Center};

struct SceneGizmoTransform {
    GameSproutRuntime::Vector3 Position;
    GameSproutRuntime::Quaternion Rotation;
    GameSproutRuntime::Vector3 Scale{1,1,1};
};
struct SceneGizmoTarget {
    SceneGizmoTransform Local;
    GameSproutRuntime::Mat4 ParentWorld;
    // 沿父链 localRotation 连乘的朝向，不含缩放。不可从可能带剪切的 ParentWorld 反解。
    GameSproutRuntime::Quaternion ParentRotation;
    GameSproutRuntime::Vector3 BoundsCenterWorld;
    bool HasBoundsCenter=false;
};
struct SceneGizmoView {
    GameSproutRuntime::RenderCamera Camera;
    std::uint32_t Width=0,Height=0;
};
struct SceneGizmoSettings {
    SceneGizmoTool Tool=SceneGizmoTool::Move;
    SceneGizmoSpace Space=SceneGizmoSpace::World;
    SceneGizmoPivot Pivot=SceneGizmoPivot::Pivot;
    float SizePixels=92;
    float HitRadiusPixels=8;
    float TranslationSnap=1;
    float RotationSnapDegrees=15;
    // 缩放倍率的增量，例如 .1 表示 1.0、1.1、1.2 倍；不累加到当前 scale。
    float ScaleSnap=.1f;
};

// 不持有 Runtime 对象，也不写入节点。宿主负责预览、提交和一次拖动的 Undo 事务。
// 像素以实际渲染目标为准，原点在左上。拖动固定按下时的 TRS/父矩阵/相机。
class SceneGizmo final {
public:
    bool Update(const SceneGizmoTarget& target,const SceneGizmoView& view,const SceneGizmoSettings& settings,std::string& error);
    SceneGizmoHandle Hover(GameSproutRuntime::Vector2 pixel);
    bool BeginDrag(GameSproutRuntime::Vector2 pixel,std::string& error);
    bool Drag(GameSproutRuntime::Vector2 pixel,bool snap,SceneGizmoTransform& output,std::string& error);
    void EndDrag();
    void CancelDrag();
    bool IsDragging() const {return _Dragging;}
    SceneGizmoHandle ActiveHandle() const {return _Dragging?_Active:_Hovered;}
    const SceneGizmoTransform& GetInitialTransform() const {return _Target.Local;}
    GameSproutRuntime::Vector3 GetPivotWorld() const {return _DrawPivot;}
    float GetHandleWorldSize() const {return _Length;}
    GameSproutRuntime::Vector3 GetAxis(std::size_t axis) const {return axis<3?_Axes[axis]:GameSproutRuntime::Vector3{};}
    // 向现有列表追加 depthless 几何；调用方可同时追加选择边框。
    void BuildDrawList(GameSproutRuntime::DebugDrawList& output) const;
    static bool MakeRay(const SceneGizmoView& view,GameSproutRuntime::Vector2 pixel,GameSproutRuntime::Ray& output);
    static bool Project(const SceneGizmoView& view,GameSproutRuntime::Vector3 world,GameSproutRuntime::Vector2& output);
private:
    SceneGizmoHandle HitTest(GameSproutRuntime::Vector2 pixel) const;
    bool ValidateCandidate(const SceneGizmoTransform& candidate,std::string& error) const;
    bool SetCenteredPosition(SceneGizmoTransform& candidate,std::string& error) const;
    SceneGizmoTarget _Target;
    SceneGizmoView _View;
    SceneGizmoSettings _Settings;
    GameSproutRuntime::Mat4 _World,_InverseParent;
    GameSproutRuntime::Vector3 _Axes[3]={{1,0,0},{0,1,0},{0,0,1}};
    GameSproutRuntime::Vector3 _CameraRight{1,0,0},_CameraUp{0,1,0},_CameraForward{0,0,-1};
    GameSproutRuntime::Vector3 _Pivot,_DrawPivot,_LocalPivot,_PlaneNormal,_StartPlanePoint,_RotationStart,_RotationAxis,_LinearRotationDirection;
    GameSproutRuntime::Vector2 _StartPixel;
    float _Length=1;
    double _PreviousAngle=0,_AngleTurns=0;
    bool _Valid=false,_Dragging=false,_LinearRotation=false;
    SceneGizmoHandle _Hovered=SceneGizmoHandle::None,_Active=SceneGizmoHandle::None;
};
