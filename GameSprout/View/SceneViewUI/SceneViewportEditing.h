#pragma once
#include "SceneGizmo.h"
#include "ISceneManager.h"
#include "ISelectionManager.h"
#include <functional>

class SceneViewport;
enum class SceneEditingTool { View, Move, Rotate, Scale };

// Scene 编辑交互：只借用服务，长期只保存节点 ID；变换历史由 SceneManager 持有。
class SceneViewportEditing final {
public:
    explicit SceneViewportEditing(SceneViewport& viewport):_Viewport(viewport) {}
    void Bind(ISceneManager* scenes,ISelectionManager* selection);
    void Unbind();
    void UpdateFrame(GameSproutRuntime::IScene* scene,const GameSproutRuntime::RenderCamera* drawCamera=nullptr);
    void BuildDraw(GameSproutRuntime::IScene* scene,GameSproutRuntime::DebugDrawList& draw);
    bool Press(GameSproutRuntime::Vector2 logicalPixel);
    bool Move(GameSproutRuntime::Vector2 logicalPixel,bool snap);
    bool Release(GameSproutRuntime::Vector2 logicalPixel,bool snap);
    void Cancel();
    bool DeleteSelection();
    bool IsDragging() const {return _Gizmo.IsDragging();}
    SceneEditingTool Tool() const {return _Tool;}
    void SetTool(SceneEditingTool tool);
    void SetSpace(SceneGizmoSpace space);
    void SetPivot(SceneGizmoPivot pivot);
    void SetSnap(bool enabled,float move,float rotation,float scale);
    std::function<void(SceneEditingTool)> ToolChanged;
private:
    bool View(SceneGizmoView& view) const;
    GameSproutRuntime::Vector2 Pixel(GameSproutRuntime::Vector2 logical,const SceneGizmoView& view) const;
    GameSproutRuntime::IGameObject* Selected() const;
    void Report(const std::string& error);
    SceneViewport& _Viewport;
    ISceneManager* _Scenes=nullptr;
    ISelectionManager* _Selection=nullptr;
    std::uint64_t _SceneID=0,_ObjectID=0;
    SceneEditingTool _Tool=SceneEditingTool::Move;
    SceneGizmoSettings _Settings;
    SceneGizmo _Gizmo;
    SceneGizmoView _DragView;
    GameSproutRuntime::Vector2 _HoverPixel;
    bool _HasHover=false;
    bool _Ready=false,_Snap=false;
};
