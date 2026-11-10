#pragma once
#include "IUIAuthoring.h"
#include "RuntimeRenderApi.h"
#include <QtCore/Qt>

// 操作手柄使用 Runtime 提供的投影几何。拖动过程中仅保存值快照。
class SceneUIRectTool {
public:
    enum class Mode { Outline, Move, Rect };
    enum class Handle { None, X, Y, XY, Inside, Left, Top, Right, Bottom, TopLeft, TopRight, BottomRight, BottomLeft, Rotate };

    void Configure(const GameSproutRuntime::UIScreenPreviewGeometry& geometry, float pixelsPerPoint,
                   Mode mode, bool local, bool center);
    void Clear();
    void BuildDraw(GameSproutRuntime::UIDebugDrawList& draw) const;
    Handle Hit(GameSproutRuntime::Vector2 pixel) const;
    bool Hover(GameSproutRuntime::Vector2 pixel);
    void ClearHover() { _Hover=Handle::None; }
    Qt::CursorShape Cursor() const;
    static bool IsResize(Handle handle);

    void Begin(Handle handle, GameSproutRuntime::Vector2 pixel, GameSproutRuntime::Vector2 pivot,
               const UIRectAuthoringValues& initial);
    bool Drag(GameSproutRuntime::Vector2 pixel, bool shift, bool alt, float moveSnap,
              float rotateSnap, UIRectAuthoringValues& result);
    void End();
    bool IsDragging() const { return _Active!=Handle::None; }
    bool HasMoved() const { return _Moved; }
    Handle ActiveHandle() const { return _Active; }
    const UIRectAuthoringValues& LatestValues() const { return _Latest; }

private:
    GameSproutRuntime::Vector2 MoveOrigin() const;
    GameSproutRuntime::Vector2 MoveAxis(bool horizontal) const;
    bool DragMove(GameSproutRuntime::Vector2 delta, bool shift, float snap, UIRectAuthoringValues& result) const;
    bool DragResize(GameSproutRuntime::Vector2 delta, bool shift, bool alt, float snap, UIRectAuthoringValues& result) const;
    bool DragRotate(GameSproutRuntime::Vector2 pixel, float snap, UIRectAuthoringValues& result);

    GameSproutRuntime::UIScreenPreviewGeometry _Geometry, _Start;
    GameSproutRuntime::Vector2 _StartPixel, _Pivot, _AxisX, _AxisY;
    UIRectAuthoringValues _Initial, _Latest;
    float _PixelsPerPoint=1.0f, _StartPixelsPerPoint=1.0f;
    float _PreviousAngle=0, _Rotation=0;
    Mode _Mode=Mode::Outline;
    Handle _Hover=Handle::None, _Active=Handle::None;
    bool _Ready=false, _Local=false, _Center=false, _Moved=false;
};
