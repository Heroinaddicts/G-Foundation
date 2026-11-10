#pragma once
#include "SceneGizmo.h"
#include "SceneUIRectTool.h"
#include "ISceneManager.h"
#include "ISelectionManager.h"
#include <functional>
#include <optional>

class SceneViewport;
enum class SceneEditingTool { View, Move, Rotate, Scale, Rect };

// Scene 编辑交互：只借用服务，长期只保存节点 ID；变换历史由 SceneManager 持有。
class SceneViewportEditing final : public ISceneListener, public ISelectionListener {
public:
    explicit SceneViewportEditing(SceneViewport& viewport):_Viewport(viewport) {}
    void Bind(ISceneManager* scenes,ISelectionManager* selection,IUIAuthoring* uiAuthoring=nullptr);
    void OnActiveSceneChanged() override;
    void OnSceneStateChanged() override;
    void OnAssetSelectionChanged(const std::vector<ProjectAssetSelection>&) override;
    void OnSceneObjectSelectionChanged(const SceneObjectSelection&) override;
    void Unbind();
    void UpdateFrame(GameSproutRuntime::IScene* scene,const GameSproutRuntime::RenderCamera* drawCamera=nullptr);
    void BuildDraw(GameSproutRuntime::IScene* scene,GameSproutRuntime::DebugDrawList& draw);
    void BuildUIDraw(GameSproutRuntime::IScene* scene,std::uint64_t previewObjectID,GameSproutRuntime::UIDebugDrawList& draw);
    std::uint64_t UIScreenPreviewObjectID() const;
    bool Press(GameSproutRuntime::Vector2 logicalPixel,bool alt=false);
    bool Move(GameSproutRuntime::Vector2 logicalPixel,bool snap,bool shift=false,bool alt=false);
    void ApplyPendingDrag();
    bool Release(GameSproutRuntime::Vector2 logicalPixel,bool snap,std::uint64_t& shadowPreviewID,bool shift=false,bool alt=false);
    void Cancel();
    void CancelUIDrag();
    void ClearHover();
    void SetReadOnly(bool value);
    bool DeleteSelection();
    bool IsDragging() const {return _Gizmo.IsDragging()||_UIRectTool.IsDragging();}
    bool IsDraggingUI() const {return _UIRectTool.IsDragging();}
    std::uint64_t DraggingObjectID() const {return IsDragging()?_ObjectID:0;}
    std::uint64_t ShadowPreviewObjectID() const {return _DragChanged?DraggingObjectID():0;}
    SceneEditingTool Tool() const {return _Tool;}
    void SetTool(SceneEditingTool tool);
    void SetSpace(SceneGizmoSpace space);
    void SetPivot(SceneGizmoPivot pivot);
    void SetSnap(bool enabled,float move,float rotation,float scale);
    void SetShowAllColliders(bool enabled);
    void SetShowChildColliders(bool enabled);
    bool ShowAllColliders() const {return _ShowAllColliders;}
    void SetExternalToolActive(bool value);
    std::function<void(SceneEditingTool)> ToolChanged;
private:
    bool View(SceneGizmoView& view) const;
    GameSproutRuntime::Vector2 Pixel(GameSproutRuntime::Vector2 logical,const SceneGizmoView& view) const;
    GameSproutRuntime::IGameObject* Selected() const;
    bool PickUI(const SceneGizmoView&,GameSproutRuntime::Vector2 pixel);
    void ConfigureUITool(GameSproutRuntime::IScene*,std::uint64_t,const SceneGizmoView&,GameSproutRuntime::Vector2 layoutSize);
    bool PressUI(const SceneGizmoView&,GameSproutRuntime::Vector2 pixel,bool alt);
    bool MoveUI(GameSproutRuntime::Vector2 logical,bool snap,bool shift,bool alt);
    bool ReleaseUI(GameSproutRuntime::Vector2 logical,bool snap,bool shift,bool alt);
    void ApplyPendingUIDrag();
    void UpdateUICursor();
    void Report(const std::string& error);
    SceneViewport& _Viewport;
    ISceneManager* _Scenes=nullptr;
    ISelectionManager* _Selection=nullptr;
    IUIAuthoring* _UIAuthoring=nullptr;
    GameSproutRuntime::IScene* _ActiveScene=nullptr;
    SceneObjectSelection _SelectionSnapshot;
    bool _SceneSubscribed=false,_SelectionSubscribed=false,_TransformEditActive=false;
    std::uint64_t _SceneID=0,_ObjectID=0;
    SceneEditingTool _Tool=SceneEditingTool::Move;
    SceneGizmoSettings _Settings;
    SceneGizmo _Gizmo;
    SceneUIRectTool _UIRectTool;
    SceneGizmoView _DragView;
    std::optional<SceneTransformState> _PendingDrag;
    std::optional<UIRectAuthoringValues> _PendingUIDrag;
    bool _DragChanged=false;
    bool _UIDragPreviewQueued=false;
    GameSproutRuntime::Vector2 _HoverPixel;
    bool _HasHover=false;
    bool _Ready=false,_Snap=false,_ReadOnly=false;
    bool _ShowAllColliders=false;
    bool _ShowChildColliders=false;
    bool _ExternalToolActive=false;
};
