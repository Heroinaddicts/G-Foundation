#include "ColliderSceneTool.h"

ColliderSceneTool::ColliderSceneTool(GameSproutRuntime::IGameSproutRuntime* runtime,IPhysicsAuthoring* authoring,ISceneViewUI* view)
    :_Runtime(runtime),_Authoring(authoring),_View(view) {}
ColliderSceneTool::~ColliderSceneTool(){CancelSceneToolDrag();_Listeners.clear();}
bool ColliderSceneTool::ToggleTarget(const ColliderEditTarget& target) {
    _Error.clear();
    if(IsTargetActive(target)){_View->DeactivateSceneTool(this);return true;}
    if(!_Runtime||!_Authoring||!_View){_Error="Scene 碰撞体编辑服务尚未就绪。";return false;}
    ColliderEditState state;
    if(!_Authoring->CanEditCollider(target,_Error)||!_Authoring->ReadCollider(target,state,_Error))return false;
    if(state.Kind==ColliderShapeKind::Mesh){_Error="Mesh Collider 的碰撞形状由引用网格决定，不支持基础尺寸手柄。";return false;}
    auto* object=_Runtime->QueryGameObject(target.ObjectID);
    GameSproutRuntime::RuntimePhysicsMath::ColliderFrame frame;
    const auto rotation=state.Kind==ColliderShapeKind::Sphere?GameSproutRuntime::Quaternion{}:state.Settings.Rotation;
    if(!object||!GameSproutRuntime::RuntimePhysicsMath::TryGetColliderFrame(object->GetTransform()->GetWorldMatrix()
        *GameSproutRuntime::RuntimeMath::ComposeTRS(state.Settings.Center,rotation,{1,1,1}),frame)) {
        _Error="碰撞体编辑需要非零缩放且不含剪切的变换。";return false;
    }
    if(_Active)_View->DeactivateSceneTool(this);
    _Target=target;
    if(!_View->ActivateSceneTool(this)){_Error="Scene 工具尚未注册或窗口不可用。";return false;}
    return true;
}
bool ColliderSceneTool::IsTargetActive(const ColliderEditTarget& target) const {return _Active&&_Target==target;}
bool ColliderSceneTool::RegisterListener(const void* owner,const std::function<void()>& callback) {
    return owner&&callback&&_Listeners.emplace(owner,callback).second;
}
void ColliderSceneTool::UnregisterListener(const void* owner){_Listeners.erase(owner);}
void ColliderSceneTool::Changed() {
    const auto snapshot=_Listeners;
    for(const auto& [owner,callback]:snapshot)if(_Listeners.count(owner))callback();
}
void ColliderSceneTool::OnSceneToolActivated(bool active) {
    if(_Active==active)return;
    _Active=active;if(!active){CancelSceneToolDrag();_Target={};}
    Changed();
}
bool ColliderSceneTool::Update(const SceneToolContext& context) {
    if(!_Active||!_Runtime||!_Authoring||context.SceneID!=_Target.SceneID)return false;
    ColliderEditState state;
    auto* object=_Runtime->QueryGameObject(_Target.ObjectID);
    if(!object||!object->GetScene()||object->GetScene()->GetInstanceID()!=_Target.SceneID
        ||!_Authoring->ReadCollider(_Target,state,_Error)) {
        _View->DeactivateSceneTool(this);return false;
    }
    if(_Geometry.IsDragging()&&!_Authoring->HasColliderEdit())_Geometry.End();
    return _Geometry.Update(state,object->GetTransform()->GetWorldMatrix(),context,_Error);
}
void ColliderSceneTool::DrawSceneTool(const SceneToolContext& context,GameSproutRuntime::DebugDrawList& draw) {
    if(Update(context))_Geometry.Draw(draw);
}
bool ColliderSceneTool::PressSceneTool(const SceneToolContext& context,const SceneToolPointer& pointer) {
    if(!Update(context)||_Geometry.Hover(pointer.Pixel)<0)return false;
    if(!_Authoring->BeginColliderEdit(_Target,_Error))return true;
    if(!_Geometry.Begin(pointer.Pixel,_Error)){std::string ignored;_Authoring->CancelColliderEdit(ignored);}
    return true;
}
bool ColliderSceneTool::MoveSceneTool(const SceneToolContext& context,const SceneToolPointer& pointer) {
    if(!_Geometry.IsDragging()) {if(Update(context))_Geometry.Hover(pointer.Pixel);return false;}
    if(!_Authoring->HasColliderEdit()){_Geometry.End();return true;}
    ColliderEditState desired;
    if(_Geometry.Drag(pointer,desired,_Error)&&_Authoring->PreviewColliderEdit(desired,_Error))return true;
    // Keep the most recent valid preview; release/cancel still completes the
    // original transaction and never converts the edit into Transform scaling.
    return true;
}
bool ColliderSceneTool::ReleaseSceneTool(const SceneToolContext& context,const SceneToolPointer& pointer) {
    if(!_Geometry.IsDragging())return false;
    MoveSceneTool(context,pointer);
    _Geometry.End();
    if(!_Authoring->CommitColliderEdit(_Error)){std::string ignored;_Authoring->CancelColliderEdit(ignored);}
    return true;
}
void ColliderSceneTool::CancelSceneToolDrag() {
    if(!_Geometry.IsDragging())return;
    _Geometry.End();
    if(_Authoring){std::string error;if(!_Authoring->CancelColliderEdit(error))_Error=error;}
}
