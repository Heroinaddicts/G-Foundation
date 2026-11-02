#include "ColliderSceneTool.h"
#include "GameSproutRuntimeExport.h"
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value,const char* error){if(!value)throw std::runtime_error(error);}
class Authoring final : public IPhysicsAuthoring {
public:
    ColliderEditTarget Target;
    ColliderEditState State,Initial;
    bool Dragging=false;
    unsigned Begins=0,Previews=0,Commits=0,Cancels=0;
    bool ReadCollider(const ColliderEditTarget& target,ColliderEditState& output,std::string& error) const override {
        error.clear();if(target!=Target){error="stale target";return false;}output=State;return true;
    }
    bool CanEditCollider(const ColliderEditTarget& target,std::string& error) const override {error.clear();return target==Target&&!Dragging;}
    bool BeginColliderEdit(const ColliderEditTarget& target,std::string& error) override {
        if(!CanEditCollider(target,error))return false;Initial=State;Dragging=true;++Begins;return true;
    }
    bool PreviewColliderEdit(const ColliderEditState& value,std::string&) override {if(!Dragging)return false;State=value;++Previews;return true;}
    bool CommitColliderEdit(std::string&) override {if(!Dragging)return false;Dragging=false;++Commits;return true;}
    bool CancelColliderEdit(std::string&) override {if(Dragging){State=Initial;Dragging=false;++Cancels;}return true;}
    bool HasColliderEdit() const override {return Dragging;}
    bool CreatePhysicsMaterialAsset(const std::string&,const std::string&,std::string&,std::string&) override{return false;}
    bool LoadPhysicsMaterialSource(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::PhysicsMaterialData&,std::string&,std::string&) const override{return false;}
    bool SavePhysicsMaterialSource(const GameSproutRuntime::ResourceReference&,const GameSproutRuntime::PhysicsMaterialData&,const std::string&,std::string&) override{return false;}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class View final : public ISceneViewUI {
public:
    ISceneTool* Active=nullptr;
    SceneViewCameraState GetCameraState() const override{return {};}
    bool SetCameraState(const SceneViewCameraState&) override{return false;}
    void ResetCamera() override{}
    bool FocusSelection() override{return false;}
    bool IsRenderingAvailable() const override{return true;}
    std::string GetRenderError() const override{return {};}
    bool ActivateSceneTool(ISceneTool* tool) override {DeactivateSceneTool(Active);Active=tool;if(tool)tool->OnSceneToolActivated(true);return tool;}
    void DeactivateSceneTool(ISceneTool* tool) override {if(Active&&Active==tool){auto* old=Active;Active=nullptr;old->OnSceneToolActivated(false);}}
    ISceneTool* GetActiveSceneTool() const override{return Active;}
    void Show() override{}void Hide() override{}
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutViewApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutViewApi*) override{}
};
}
int main(int argc,char** argv) {
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        Check(argc==2,"Pass Runtime directory");runtime=Export(argv[1]);Check(runtime,"Runtime unavailable");
        auto* scene=runtime->CreateScene("Collider tool integration");auto* object=scene->CreateGameObject("Box owner");
        Authoring authoring;authoring.Target={scene->GetInstanceID(),object->GetInstanceID(),1};View view;
        ColliderSceneTool tool(runtime,&authoring,&view);unsigned notifications=0;
        Check(tool.RegisterListener(&notifications,[&]{++notifications;}),"Tool observer registration failed");
        Check(tool.ToggleTarget(authoring.Target)&&tool.IsTargetActive(authoring.Target)&&notifications==1,"Inspector command did not activate the tool or notify observers");
        SceneToolContext context;context.Runtime=runtime;context.SceneID=scene->GetInstanceID();context.Frame.SceneID=context.SceneID;
        context.Frame.Width=800;context.Frame.Height=600;context.Frame.Camera.View(2,3)=-8;
        GameSproutRuntime::DebugDrawList draw;tool.DrawSceneTool(context,draw);Check(draw.Triangles.size()==72,"Activated tool has no six face handles");
        ColliderHandleGeometry geometry;std::string error;Check(geometry.Update(authoring.State,object->GetTransform()->GetWorldMatrix(),context,error),error.c_str());
        SceneToolPointer pointer;Check(ColliderHandleGeometry::Project(context,geometry.HandlePosition(1),pointer.Pixel),"Handle projection failed");
        Check(tool.PressSceneTool(context,pointer)&&tool.IsSceneToolDragging()&&authoring.Begins==1,"Picking a Collider handle did not begin its transaction");
        pointer.Pixel.X+=35;Check(tool.MoveSceneTool(context,pointer),"First preview failed");
        pointer.Pixel.X+=35;Check(tool.MoveSceneTool(context,pointer),"Second preview failed");
        Check(authoring.State.Box.Size.X>1&&authoring.State.Settings.Center.X>0,"Tool input did not change Collider values");
        Check(tool.ReleaseSceneTool(context,pointer)&&authoring.Commits==1&&!authoring.Dragging&&!tool.IsSceneToolDragging(),"A multi-event drag did not commit exactly once");
        const auto transform=object->GetTransform()->GetLocalScale();Check(transform.X==1&&transform.Y==1&&transform.Z==1,"Collider edit changed the GameObject Transform");
        const auto saved=authoring.State;draw={};tool.DrawSceneTool(context,draw);
        Check(geometry.Update(authoring.State,object->GetTransform()->GetWorldMatrix(),context,error),error.c_str());
        Check(ColliderHandleGeometry::Project(context,geometry.HandlePosition(1),pointer.Pixel),"Updated handle projection");
        Check(tool.PressSceneTool(context,pointer),"Second transaction begin failed");pointer.Pixel.X+=60;tool.MoveSceneTool(context,pointer);
        tool.CancelSceneToolDrag();Check(authoring.Cancels==1&&authoring.State.Box.Size.X==saved.Box.Size.X,"Escape/focus cancellation did not restore the initial Collider snapshot");
        Check(tool.ToggleTarget(authoring.Target)&&!tool.IsTargetActive(authoring.Target)&&notifications==2,"Inspector toggle did not leave collider editing");
        authoring.State.Kind=ColliderShapeKind::Mesh;
        Check(!tool.ToggleTarget(authoring.Target)&&!tool.GetSceneToolError().empty(),"MeshCollider received a primitive resize mode");
        authoring.State.Kind=ColliderShapeKind::Box;Check(tool.ToggleTarget(authoring.Target),"Tool reactivation failed");
        authoring.Target.ComponentID=2;draw={};tool.DrawSceneTool(context,draw);
        Check(!view.Active&&!tool.IsTargetActive(authoring.Target),"A deleted/replaced component kept stale tool activation");
        tool.UnregisterListener(&notifications);delete runtime;runtime=nullptr;
        std::cout<<"PASS Collider tool Inspector activation, screen input, previews, one commit, cancellation, observer events and stale identities\n";return 0;
    }catch(const std::exception& error){delete runtime;std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
