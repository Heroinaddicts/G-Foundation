#include "SceneViewportEditing.h"
#include "SceneViewport.h"
#include <algorithm>

namespace {
GameSproutRuntime::Quaternion ParentRotation(GameSproutRuntime::IGameObject* parent) {
    GameSproutRuntime::Quaternion rotation;
    // 世界朝向按祖先的局部旋转组合。不能分解ParentWorld：非等比缩放与多级旋转可形成剪切。
    for(auto* node=parent;node;node=node->Parent()) {
        const auto local=node->GetTransform()->GetLocalRotation(),child=rotation;
        rotation={local.W*child.X+local.X*child.W+local.Y*child.Z-local.Z*child.Y,
            local.W*child.Y-local.X*child.Z+local.Y*child.W+local.Z*child.X,
            local.W*child.Z+local.X*child.Y-local.Y*child.X+local.Z*child.W,
            local.W*child.W-local.X*child.X-local.Y*child.Y-local.Z*child.Z};
        if(!rotation.TryNormalize(rotation))return {0,0,0,0};
    }
    return rotation;
}
}

void SceneViewportEditing::Bind(ISceneManager* scenes,ISelectionManager* selection) {
    if(_Scenes==scenes&&_Selection==selection)return;
    Cancel();_Viewport.ClearEditingError();_Scenes=scenes;_Selection=selection;_SceneID=0;_ObjectID=0;_Ready=false;
}
void SceneViewportEditing::Unbind(){Cancel();_Viewport.ClearEditingError();_Scenes=nullptr;_Selection=nullptr;_SceneID=0;_ObjectID=0;_Ready=false;}
void SceneViewportEditing::Report(const std::string& error){if(!error.empty())_Viewport.ReportEditingError(error);}
bool SceneViewportEditing::View(SceneGizmoView& view) const {
    GameSproutRuntime::RenderedCamera rendered;
    if(!_Viewport.GetRenderedCamera(rendered)||!_Scenes||!_Scenes->GetActiveScene()
        ||rendered.SceneID!=_Scenes->GetActiveScene()->GetInstanceID())return false;
    view={rendered.Camera,rendered.Width,rendered.Height};return view.Width&&view.Height;
}
GameSproutRuntime::Vector2 SceneViewportEditing::Pixel(GameSproutRuntime::Vector2 logical,const SceneGizmoView& view) const {
    return {logical.X*view.Width/std::max(1,_Viewport.width()),logical.Y*view.Height/std::max(1,_Viewport.height())};
}
GameSproutRuntime::IGameObject* SceneViewportEditing::Selected() const {
    if(!_Scenes||!_Selection||!_Viewport.GetRuntime()||!_Scenes->GetActiveScene())return nullptr;
    const auto selection=_Selection->GetSceneObjectSelection();
    auto* scene=_Scenes->GetActiveScene();
    if(selection.SceneID!=scene->GetInstanceID())return nullptr;
    auto* object=_Viewport.GetRuntime()->QueryGameObject(selection.ObjectID);
    return object&&object!=scene&&object->GetScene()==scene?object:nullptr;
}
void SceneViewportEditing::UpdateFrame(GameSproutRuntime::IScene* scene,const GameSproutRuntime::RenderCamera* drawCamera) {
    auto* object=Selected();
    const auto sceneID=scene?scene->GetInstanceID():0,objectID=object?object->GetInstanceID():0;
    if(sceneID!=_SceneID||objectID!=_ObjectID) {Cancel();_Ready=false;_SceneID=sceneID;_ObjectID=objectID;}
    if(_Gizmo.IsDragging()) {
        if(!_Scenes||!_Scenes->HasTransformEdit())Cancel();
        return;
    }
    _Ready=false;
    if(!object||_Tool==SceneEditingTool::View||!_Viewport.Renderer())return;
    SceneGizmoView view;if(!View(view))return;
    // 绘制共享本帧RenderCamera，输入仍用最近成功呈现帧的相机。
    if(drawCamera)view.Camera=*drawCamera;
    auto* transform=object->GetTransform();
    SceneGizmoTarget target;target.Local={transform->GetLocalPosition(),transform->GetLocalRotation(),transform->GetLocalScale()};
    if(auto* parent=object->Parent()){target.ParentWorld=parent->GetTransform()->GetWorldMatrix();target.ParentRotation=ParentRotation(parent);}
    GameSproutRuntime::Bounds bounds;
    if(_Viewport.Renderer()->GetWorldBounds(scene,objectID,bounds)){target.HasBoundsCenter=true;target.BoundsCenterWorld=bounds.Center;}
    // 按逻辑像素保持工具大小；实际几何和命中统一使用最近成功帧的物理像素。
    auto settings=_Settings;const float dpi=float(view.Height)/std::max(1,_Viewport.height());
    settings.SizePixels*=dpi;settings.HitRadiusPixels*=dpi;
    std::string error;_Ready=_Gizmo.Update(target,view,settings,error);
    if(_Ready&&_HasHover)_Gizmo.Hover(Pixel(_HoverPixel,view));
}
void SceneViewportEditing::BuildDraw(GameSproutRuntime::IScene* scene,GameSproutRuntime::DebugDrawList& draw) {
    auto* object=Selected();auto* renderer=_Viewport.Renderer();
    if(object&&renderer)renderer->AppendSelectionOutline(scene,object->GetInstanceID(),draw);
    if(_Ready&&object&&_Tool!=SceneEditingTool::View)_Gizmo.BuildDrawList(draw);
}
bool SceneViewportEditing::Press(GameSproutRuntime::Vector2 logical) {
    if(!_Scenes||!_Selection||!_Viewport.Renderer()||!_Scenes->GetActiveScene())return false;
    UpdateFrame(_Scenes->GetActiveScene());
    _Viewport.ClearEditingError();
    SceneGizmoView view;if(!View(view))return false;
    const auto pixel=Pixel(logical,view);
    if(_Ready&&_Gizmo.Hover(pixel)!=SceneGizmoHandle::None) {
        std::string error;
        if(!_Gizmo.BeginDrag(pixel,error)){Report(error);return true;}
        const char* label=_Tool==SceneEditingTool::Move?"Scene 移动物体":_Tool==SceneEditingTool::Rotate?"Scene 旋转物体":"Scene 缩放物体";
        if(!_Scenes->BeginTransformEdit(_ObjectID,label,error)){_Gizmo.CancelDrag();Report(error);return true;}
        _DragView=view;return true;
    }
    GameSproutRuntime::Ray ray;GameSproutRuntime::SceneRaycastHit hit;
    bool picked=SceneGizmo::MakeRay(view,pixel,ray);
    if(picked&&view.Camera.Projection.Projection==GameSproutRuntime::CameraProjection::Perspective) {
        // 从近裁剪面开始，避免相机与近裁剪面之间不可见的三角形挡住可见对象。
        const auto direction=view.Camera.View*GameSproutRuntime::Vector4{ray.Direction.X,ray.Direction.Y,ray.Direction.Z,0};
        picked=direction.Z<0;
        if(picked)ray.Origin=ray.GetPoint(view.Camera.Projection.NearPlane/-direction.Z);
    }
    picked=picked&&_Viewport.Renderer()->Raycast(_Scenes->GetActiveScene(),ray,hit,view.Camera.Projection.CullingMask);
    if(picked) {
        const auto cameraPoint=view.Camera.View*GameSproutRuntime::Vector4{hit.Position.X,hit.Position.Y,hit.Position.Z,1};
        picked=-cameraPoint.Z<=view.Camera.Projection.FarPlane;
    }
    if(picked)
        _Selection->SetSceneObjectSelection({_Scenes->GetActiveScene()->GetInstanceID(),hit.ObjectID});
    else _Selection->SetSceneObjectSelection({});
    _Ready=false;return true;
}
bool SceneViewportEditing::Move(GameSproutRuntime::Vector2 logical,bool snap) {
    _HoverPixel=logical;_HasHover=true;
    if(_Gizmo.IsDragging()) {
        if(!_Scenes||!Selected()||Selected()->GetInstanceID()!=_ObjectID||!_Scenes->HasTransformEdit()){Cancel();return true;}
        // 拖动时相机和目标像素尺寸都固定。宿主 resize 会先取消此事务。
        SceneGizmoTransform candidate;std::string error;
        if(_Gizmo.Drag(Pixel(logical,_DragView),snap||_Snap,candidate,error)) {
            _Viewport.ClearEditingError();
            if(!_Scenes->PreviewTransformEdit({candidate.Position,candidate.Rotation,candidate.Scale},error)){Report(error);Cancel();}
        }else Report(error);
        return true;
    }
    SceneGizmoView view;
    if(_Ready&&View(view))_Gizmo.Hover(Pixel(logical,view));
    return false;
}
bool SceneViewportEditing::Release(GameSproutRuntime::Vector2 logical,bool snap) {
    if(!_Gizmo.IsDragging())return false;
    Move(logical,snap);
    if(_Gizmo.IsDragging()) {
        _Gizmo.EndDrag();std::string error;
        if(_Scenes&&!_Scenes->CommitTransformEdit(error)){Report(error);_Scenes->CancelTransformEdit(error);}
    }
    _Ready=false;return true;
}
void SceneViewportEditing::Cancel() {
    const bool dragging=_Gizmo.IsDragging();_Gizmo.CancelDrag();_Ready=false;
    if(dragging&&_Scenes){std::string error;if(!_Scenes->CancelTransformEdit(error))Report(error);}
    if(dragging)_Viewport.ReleaseEditingCapture();
}
bool SceneViewportEditing::DeleteSelection() {
    if(!_Scenes||!_Selection)return false;
    if(_Gizmo.IsDragging()||_Scenes->HasTransformEdit()) {Report("请先完成或取消 Scene 中的变换拖拽。");return true;}
    auto* object=Selected();if(!object)return true;
    _Viewport.ClearEditingError();
    std::string error;
    if(!_Scenes->DeleteObject(object->GetInstanceID(),error)) {Report(error);return true;}
    const auto selection=_Selection->GetSceneObjectSelection();
    if(!_Viewport.GetRuntime()->QueryGameObject(selection.ObjectID))_Selection->SetSceneObjectSelection({});
    _Gizmo.CancelDrag();_Ready=false;_ObjectID=0;
    return true;
}
void SceneViewportEditing::SetTool(SceneEditingTool tool) {
    Cancel();_Viewport.ClearEditingError();_Tool=tool;
    if(tool!=SceneEditingTool::View)_Settings.Tool=tool==SceneEditingTool::Move?SceneGizmoTool::Move:tool==SceneEditingTool::Rotate?SceneGizmoTool::Rotate:SceneGizmoTool::Scale;
    if(ToolChanged)ToolChanged(tool);
}
void SceneViewportEditing::SetSpace(SceneGizmoSpace value){Cancel();_Viewport.ClearEditingError();_Settings.Space=value;}
void SceneViewportEditing::SetPivot(SceneGizmoPivot value){Cancel();_Viewport.ClearEditingError();_Settings.Pivot=value;}
void SceneViewportEditing::SetSnap(bool enabled,float move,float rotation,float scale){Cancel();_Viewport.ClearEditingError();_Snap=enabled;_Settings.TranslationSnap=move;_Settings.RotationSnapDegrees=rotation;_Settings.ScaleSnap=scale;}
