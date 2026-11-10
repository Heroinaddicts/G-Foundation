#include "SceneViewportEditing.h"
#include "SceneViewport.h"
#include "IObjectEditing.h"
#include <QCursor>
#include <QMetaObject>
#include <QPixmap>
#include <algorithm>

namespace {
GameSproutRuntime::Vector2 ViewSize(const SceneGizmoView& view) { return {float(view.Width),float(view.Height)}; }
}

void SceneViewportEditing::ConfigureUITool(GameSproutRuntime::IScene* scene,std::uint64_t preview,const SceneGizmoView& view,
    GameSproutRuntime::Vector2 layoutSize) {
    auto* runtime=_Viewport.GetRuntime();
    GameSproutRuntime::UIScreenPreviewGeometry geometry;
    if(!runtime||!scene||!preview||!runtime->GetUIScreenPreviewGeometry(scene,preview,ViewSize(view),geometry,layoutSize)) {
        _UIRectTool.Clear();
        return;
    }
    auto mode=SceneUIRectTool::Mode::Outline;
    if(!_ReadOnly&&!_ExternalToolActive&&_UIAuthoring) {
        if(_Tool==SceneEditingTool::Move)mode=SceneUIRectTool::Mode::Move;
        if(_Tool==SceneEditingTool::Rect)mode=SceneUIRectTool::Mode::Rect;
    }
    _UIRectTool.Configure(geometry,float(view.Height)/std::max(1,_Viewport.height()),mode,
        _Settings.Space==SceneGizmoSpace::Local,_Settings.Pivot==SceneGizmoPivot::Center);
}

void SceneViewportEditing::BuildUIDraw(GameSproutRuntime::IScene* scene,std::uint64_t preview,GameSproutRuntime::UIDebugDrawList& draw) {
    SceneGizmoView view;
    if(!View(view))return;
    ConfigureUITool(scene,preview,view,_Viewport.GetGameUILayoutSize());
    _UIRectTool.BuildDraw(draw);
}

bool SceneViewportEditing::PickUI(const SceneGizmoView& view,GameSproutRuntime::Vector2 pixel) {
    const auto preview=_Viewport.GetPresentedUIPreviewObjectID();
    if(!preview)return false;
    const auto hit=_Viewport.GetRuntime()->PickUIScreenPreview(_ActiveScene,preview,ViewSize(view),pixel,_Viewport.GetPresentedGameUILayoutSize());
    if(!hit)return false;
    const SceneObjectSelection selection{_ActiveScene->GetInstanceID(),hit};
    _Selection->SetSceneObjectSelection(selection);
    OnSceneObjectSelectionChanged(selection);
    return true;
}

bool SceneViewportEditing::PressUI(const SceneGizmoView& view,GameSproutRuntime::Vector2 pixel,bool alt) {
    if(!_UIAuthoring||_ReadOnly||_ExternalToolActive||IsDragging())return false;
    const auto preview=_Viewport.GetPresentedUIPreviewObjectID();
    if(!preview||preview!=_ObjectID)return false;
    ConfigureUITool(_ActiveScene,preview,view,_Viewport.GetPresentedGameUILayoutSize());
    const auto handle=_UIRectTool.Hit(pixel);
    if(handle==SceneUIRectTool::Handle::None||(alt&&!SceneUIRectTool::IsResize(handle)))return false;
    auto* object=Selected();
    auto* rect=object?object->GetComponent<GameSproutRuntime::IRectTransformComponent>():nullptr;
    if(!rect)return false;
    std::string error;
    const ObjectEditTarget target{_SceneID,_ObjectID,rect->GetComponentInstanceID()};
    if(!_UIAuthoring->BeginRectEdit(target,error)){Report(error);return true;}
    const UIRectAuthoringValues values{rect->GetAnchoredPosition(),rect->GetSizeDelta(),object->GetTransform()->GetLocalRotation()};
    _UIRectTool.Begin(handle,pixel,rect->GetPivot(),values);
    _DragView=view;
    UpdateUICursor();
    return true;
}

void SceneViewportEditing::UpdateUICursor() {
    const auto cursor=_UIRectTool.Cursor();
    if(cursor==Qt::CrossCursor) {
        const QPixmap image(QStringLiteral(":/SceneView/rotate-cursor.svg"));
        if(!image.isNull()) {
            _Viewport.setCursor(QCursor(image,12,12));
            return;
        }
    }
    if(cursor==Qt::ArrowCursor)_Viewport.unsetCursor();
    else _Viewport.setCursor(cursor);
}

bool SceneViewportEditing::MoveUI(GameSproutRuntime::Vector2 logical,bool snap,bool shift,bool alt) {
    if(_UIRectTool.IsDragging()) {
        UIRectAuthoringValues candidate;
        const bool snapping=_Snap||snap;
        if(_UIRectTool.Drag(Pixel(logical,_DragView),shift,alt,snapping?_Settings.TranslationSnap:0,
            snapping?_Settings.RotationSnapDegrees:0,candidate)) {
            _PendingUIDrag=candidate;
            if(!_UIDragPreviewQueued) {
                _UIDragPreviewQueued=true;
                // ObjectEditing must run outside RuntimeHost's frame callback.
                QMetaObject::invokeMethod(&_Viewport,[this] {
                    _UIDragPreviewQueued=false;
                    ApplyPendingUIDrag();
                },Qt::QueuedConnection);
            }
        }
        return true;
    }
    SceneGizmoView view;
    if(View(view)&&_UIRectTool.Hover(Pixel(logical,view))) {
        UpdateUICursor();
        _Viewport.RequestRender();
    }
    return false;
}

void SceneViewportEditing::ApplyPendingUIDrag() {
    if(!_PendingUIDrag||!_UIRectTool.IsDragging()||!_UIAuthoring)return;
    const auto candidate=*_PendingUIDrag;
    _PendingUIDrag.reset();
    std::string error;
    if(!_UIAuthoring->PreviewRectEdit(candidate,error)) {
        CancelUIDrag();
        Report(error);
        return;
    }
    _Viewport.ClearEditingError();
    _Viewport.RequestRender();
}

bool SceneViewportEditing::ReleaseUI(GameSproutRuntime::Vector2 logical,bool snap,bool shift,bool alt) {
    if(!_UIRectTool.IsDragging())return false;
    const auto handle=_UIRectTool.ActiveHandle();
    MoveUI(logical,snap,shift,alt);
    const bool moved=_UIRectTool.HasMoved();
    const auto values=_UIRectTool.LatestValues();
    _UIRectTool.End();
    _PendingUIDrag.reset();
    std::string error;
    if(moved) {
        if(!_UIAuthoring->CommitRectEdit(values,error)) {
            std::string cancelError;
            _UIAuthoring->CancelRectEdit(cancelError);
            Report(error.empty()?cancelError:error);
        }
    } else {
        if(!_UIAuthoring->CancelRectEdit(error))Report(error);
        // A click on a selected parent can still select a child under the mouse.
        if(handle==SceneUIRectTool::Handle::Inside)PickUI(_DragView,Pixel(logical,_DragView));
    }
    _Viewport.RequestRender();
    return true;
}

void SceneViewportEditing::CancelUIDrag() {
    _PendingUIDrag.reset();
    const bool dragging=_UIRectTool.IsDragging();
    const bool clearCursor=_UIRectTool.Cursor()!=Qt::ArrowCursor;
    _UIRectTool.End();
    _UIRectTool.Clear();
    if(clearCursor&&!dragging)_Viewport.unsetCursor();
    if(dragging&&_UIAuthoring) {
        std::string error;
        if(!_UIAuthoring->CancelRectEdit(error))Report(error);
    }
    if(dragging){_Viewport.ReleaseEditingCapture();_Viewport.RequestRender();}
}

void SceneViewportEditing::ClearHover() {
    _HasHover=false;
    const bool clearCursor=_UIRectTool.Cursor()!=Qt::ArrowCursor;
    _UIRectTool.ClearHover();
    if(!IsDragging()&&clearCursor) {
        _Viewport.unsetCursor();
        _Viewport.RequestRender();
    }
}
