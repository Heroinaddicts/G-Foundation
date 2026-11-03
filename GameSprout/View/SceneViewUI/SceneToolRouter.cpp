#include "SceneToolRouter.h"
#include "SceneViewport.h"
#include "SceneViewportEditing.h"
#include <algorithm>
#include <cmath>

SceneToolRouter::~SceneToolRouter(){Shutdown();}
void SceneToolRouter::Bind(ISceneManager* scenes,ISelectionManager* selection) {
    if(_Scenes==scenes&&_Selection==selection)return;
    Shutdown();_Scenes=scenes;_Selection=selection;
    if(_Scenes)_SceneSubscribed=_Scenes->RegisterSceneListener(this);
    if(_Selection)_SelectionSubscribed=_Selection->RegisterSelectionListener(this);
}
void SceneToolRouter::Shutdown() {
    if(_SceneSubscribed&&_Scenes)_Scenes->UnregisterSceneListener(this);
    if(_SelectionSubscribed&&_Selection)_Selection->UnregisterSelectionListener(this);
    _SceneSubscribed=false;_SelectionSubscribed=false;Deactivate();_Tools.clear();_Scenes=nullptr;_Selection=nullptr;
}
bool SceneToolRouter::Register(const void* owner,ISceneTool* tool) {
    return owner&&tool&&_Tools.emplace(tool,owner).second;
}
bool SceneToolRouter::Activate(ISceneTool* tool) {
    if(!tool||!_Tools.count(tool))return false;
    if(_Active==tool)return true;
    Deactivate();_Viewport.Editing().Cancel();_Active=tool;
    _Viewport.Editing().SetExternalToolActive(true);tool->OnSceneToolActivated(true);return true;
}
void SceneToolRouter::Deactivate(ISceneTool* tool) {
    if(!_Active||(tool&&tool!=_Active))return;
    auto* previous=_Active;_Active=nullptr;
    _Viewport.Editing().SetExternalToolActive(false);
    previous->CancelSceneToolDrag();previous->OnSceneToolActivated(false);
    _Viewport.ReleaseEditingCapture();
}
void SceneToolRouter::Unregister(const void* owner) {
    for(auto found=_Tools.begin();found!=_Tools.end();) {
        if(found->second==owner){if(_Active==found->first)Deactivate();found=_Tools.erase(found);}else ++found;
    }
}
void SceneToolRouter::CancelDrag() { if(_Active){const bool dragging=_Active->IsSceneToolDragging();_Active->CancelSceneToolDrag();if(dragging)_Viewport.RequestRender();} }
bool SceneToolRouter::Context(SceneToolContext& context) const {
    if(!_Active||!_Scenes||!_Viewport.GetRuntime()||!_Viewport.GetRenderedCamera(context.Frame))return false;
    auto* scene=_Scenes->GetActiveScene();
    if(!scene||context.Frame.SceneID!=scene->GetInstanceID())return false;
    context.Runtime=_Viewport.GetRuntime();context.SceneID=scene->GetInstanceID();context.SnapDistance=_SnapDistance;
    context.PixelRatio=float(context.Frame.Width)/std::max(1,_Viewport.width());return true;
}
SceneToolPointer SceneToolRouter::Pointer(GameSproutRuntime::Vector2 point,bool shift,bool alt,bool snap) const {
    GameSproutRuntime::RenderedCamera camera;_Viewport.GetRenderedCamera(camera);
    return {{point.X*camera.Width/std::max(1,_Viewport.width()),point.Y*camera.Height/std::max(1,_Viewport.height())},shift,alt,snap||_Snap};
}
void SceneToolRouter::Draw(GameSproutRuntime::IScene* scene,GameSproutRuntime::DebugDrawList& draw) {
    SceneToolContext context;if(!Context(context)||!scene||context.SceneID!=scene->GetInstanceID())return;
    auto* tool=_Active;tool->DrawSceneTool(context,draw);
    if(_Active!=tool)return;
    const auto error=tool->GetSceneToolError();if(!error.empty())_Viewport.ReportEditingError(error);
}
bool SceneToolRouter::Press(GameSproutRuntime::Vector2 point,bool shift,bool alt,bool snap) {
    if(_Active)_Viewport.RequestRender();
    SceneToolContext context;return Context(context)&&_Active->PressSceneTool(context,Pointer(point,shift,alt,snap));
}
bool SceneToolRouter::Move(GameSproutRuntime::Vector2 point,bool shift,bool alt,bool snap) {
    if(_Active)_Viewport.RequestRender();
    SceneToolContext context;return Context(context)&&_Active->MoveSceneTool(context,Pointer(point,shift,alt,snap));
}
bool SceneToolRouter::Release(GameSproutRuntime::Vector2 point,bool shift,bool alt,bool snap) {
    if(_Active)_Viewport.RequestRender();
    SceneToolContext context;return Context(context)&&_Active->ReleaseSceneTool(context,Pointer(point,shift,alt,snap));
}
