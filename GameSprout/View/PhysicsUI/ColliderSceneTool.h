#pragma once
#include "ISceneTool.h"
#include "ISceneViewUI.h"
#include "IPhysicsAuthoring.h"
#include "ColliderHandleGeometry.h"
#include <functional>
#include <map>

class ColliderSceneTool final : public ISceneTool {
public:
    ColliderSceneTool(GameSproutRuntime::IGameSproutRuntime*,IPhysicsAuthoring*,ISceneViewUI*);
    ~ColliderSceneTool() override;
    bool ToggleTarget(const ColliderEditTarget&);
    bool IsTargetActive(const ColliderEditTarget&) const;
    bool RegisterListener(const void* owner,const std::function<void()>&);
    void UnregisterListener(const void* owner);
    void OnSceneToolActivated(bool) override;
    void DrawSceneTool(const SceneToolContext&,GameSproutRuntime::DebugDrawList&) override;
    bool PressSceneTool(const SceneToolContext&,const SceneToolPointer&) override;
    bool MoveSceneTool(const SceneToolContext&,const SceneToolPointer&) override;
    bool ReleaseSceneTool(const SceneToolContext&,const SceneToolPointer&) override;
    void CancelSceneToolDrag() override;
    bool IsSceneToolDragging() const override {return _Geometry.IsDragging();}
    std::string GetSceneToolError() const override {return _Error;}
private:
    bool Update(const SceneToolContext&);
    void Changed();
    GameSproutRuntime::IGameSproutRuntime* _Runtime;
    IPhysicsAuthoring* _Authoring;
    ISceneViewUI* _View;
    ColliderEditTarget _Target;
    ColliderHandleGeometry _Geometry;
    bool _Active=false;
    std::string _Error;
    std::map<const void*,std::function<void()>> _Listeners;
};
