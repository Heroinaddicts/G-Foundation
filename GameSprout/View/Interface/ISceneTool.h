#pragma once
#include "RuntimeRenderApi.h"

// Callback-local view values. Tools retain identities/values, never Scene nodes.
struct SceneToolContext {
    GameSproutRuntime::IGameSproutRuntime* Runtime = nullptr;
    std::uint64_t SceneID = 0;
    GameSproutRuntime::RenderedCamera Frame;
    float SnapDistance = 1.0f;
    float PixelRatio = 1.0f; // Physical pixels per host logical pixel.
};
struct SceneToolPointer {
    GameSproutRuntime::Vector2 Pixel; // Render-target physical pixels.
    bool Shift = false, Alt = false, Snap = false;
};

// Feature Views implement their own tools. SceneView knows only this protocol.
class ISceneTool {
public:
    virtual ~ISceneTool() = default;
    virtual void OnSceneToolActivated(bool active) = 0;
    virtual void DrawSceneTool(const SceneToolContext&,GameSproutRuntime::DebugDrawList&) = 0;
    virtual bool PressSceneTool(const SceneToolContext&,const SceneToolPointer&) = 0;
    virtual bool MoveSceneTool(const SceneToolContext&,const SceneToolPointer&) = 0;
    virtual bool ReleaseSceneTool(const SceneToolContext&,const SceneToolPointer&) = 0;
    virtual void CancelSceneToolDrag() = 0;
    virtual bool IsSceneToolDragging() const = 0;
    virtual std::string GetSceneToolError() const { return {}; }
};
