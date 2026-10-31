#include "SceneEditingTestSupport.h"

class TransformStateListener final : public ISceneListener {
public:
    int States=0;
    void OnActiveSceneChanged() override{}
    void OnSceneStateChanged() override{++States;}
};
int main(int argc,char** argv) {
    QApplication application(argc,argv);
    try {
        TransformStateListener listener;
        SceneEditingServices fixture;fixture.Initialize();auto* scenes=fixture.Scenes;
        std::string error;
        auto* parent=scenes->GetActiveScene()->CreateGameObject("Parent");
        parent->GetTransform()->SetLocalPosition({2,3,-4});parent->GetTransform()->SetLocalScale({2,1,3});
        Check(parent->GetTransform()->SetLocalRotation({.13f,.27f,.39f,.81f}),"Cannot rotate transaction parent");
        auto* object=scenes->GetActiveScene()->CreateGameObject("Editable",parent);
        object->GetTransform()->SetLocalPosition({1.25f,-.75f,2.5f});object->GetTransform()->SetLocalScale({1.2f,.7f,-1.3f});
        Check(object->GetTransform()->SetLocalRotation({.13f,.27f,.39f,.81f}),"Cannot rotate transaction fixture");
        Check(scenes->SaveSceneAs("Scenes/Transaction.scene",error),error);
        Check(scenes->OpenScene("Scenes/Transaction.scene",error),error);fixture.Tick();
        parent=scenes->GetActiveScene()->FindChild("Parent");object=parent->FindChild("Editable");Check(object,"Reload lost transaction fixture");
        const auto initial=ReadTransform(*object);const auto originalFile=fixture.ReadScene();const auto objectID=object->GetInstanceID();
        Check(scenes->RegisterSceneListener(&listener),"Cannot listen to transform preview");
        Check(scenes->BeginTransformEdit(objectID,"Move / Rotate / Scale",error),error);
        auto intermediate=initial;intermediate.Position={2,4,-1};
        Check(scenes->PreviewTransformEdit(intermediate,error)&&CloseTransform(ReadTransform(*object),intermediate),error);
        Check(listener.States>0&&!scenes->CanUndo(),"Preview did not notify Inspector or prematurely appended Undo");
        auto final=intermediate;final.Position={3.5f,-2,4};final.Rotation={.12f,.34f,.56f,.78f};final.Scale={2.1f,.8f,-1.7f};
        Check(scenes->PreviewTransformEdit(final,error),error);final=ReadTransform(*object);
        Check(!scenes->SaveScene(error)&&fixture.ReadScene()==originalFile,"Save persisted an uncommitted preview");
        Check(!scenes->SaveSceneAs("Scenes/Preview.scene",error)&&!QFileInfo::exists(QDir(fixture.Temporary.path()).filePath("Assets/Scenes/Preview.scene")),"Save As persisted an uncommitted preview");
        auto* originalScene=scenes->GetActiveScene();
        Check(!scenes->OpenScene("Scenes/Transaction.scene",error)&&scenes->GetActiveScene()==originalScene,"Open replaced a scene during a preview");
        bool applied=false;
        Check(!scenes->EditObject(objectID,"Forbidden",[&](GameSproutRuntime::IGameObject&,std::string&){applied=true;return true;},[](GameSproutRuntime::IGameObject&,std::string&){return true;},error)&&!applied,"Discrete edit entered active transform transaction");
        Check(!scenes->Undo(error)&&!scenes->Redo(error)&&!scenes->BeginTransformEdit(parent->GetInstanceID(),"Nested",error),"Active preview accepted nested editing/history operations");
        Check(scenes->CommitTransformEdit(error)&&!scenes->HasTransformEdit()&&scenes->CanUndo(),error);
        Check(scenes->Undo(error)&&CloseTransform(ReadTransform(*object),initial)&&!scenes->CanUndo()&&scenes->CanRedo(),"One drag did not undo all TRS in one operation");
        Check(scenes->Redo(error)&&CloseTransform(ReadTransform(*object),final),"Redo did not restore the final preview");
        Check(scenes->Undo(error),error);

        // Cancel restores the exact original quaternion representation and preserves Redo.
        Check(scenes->BeginTransformEdit(objectID,"Cancel",error),error);
        Check(scenes->PreviewTransformEdit(final,error),error);
        Check(scenes->CancelTransformEdit(error)&&!scenes->HasTransformEdit()&&CloseTransform(ReadTransform(*object),initial)&&scenes->CanRedo(),"Cancel changed history or failed to restore the original TRS");
        Check(scenes->SaveScene(error)&&fixture.ReadScene()==originalFile&&!scenes->HasUnsavedChanges(),"Cancel introduced serialization drift or left a clean scene dirty");
        Check(scenes->BeginTransformEdit(objectID,"No change",error)&&scenes->CommitTransformEdit(error)&&!scenes->CanUndo()&&scenes->CanRedo(),"Zero-change drag appended history or truncated Redo");
        Check(scenes->Redo(error)&&CloseTransform(ReadTransform(*object),final),"Cancel/zero-change removed the pre-existing Redo command");
        Check(scenes->Undo(error),error);
        Check(scenes->BeginTransformEdit(objectID,"Invalid",error),error);
        auto invalid=initial;invalid.Rotation={0,0,0,0};
        Check(!scenes->PreviewTransformEdit(invalid,error)&&CloseTransform(ReadTransform(*object),initial),"Invalid quaternion partly changed the preview target");
        if(scenes->HasTransformEdit())Check(scenes->CancelTransformEdit(error),error);
        Check(scenes->BeginTransformEdit(objectID,"Non-finite",error),error);
        invalid=initial;invalid.Position.X=std::numeric_limits<float>::infinity();
        Check(!scenes->PreviewTransformEdit(invalid,error)&&CloseTransform(ReadTransform(*object),initial),"Non-finite preview partly changed the target");
        if(scenes->HasTransformEdit())Check(scenes->CancelTransformEdit(error),error);

        // A parent change invalidates the frozen drag space and rolls back only the child.
        Check(scenes->BeginTransformEdit(objectID,"Parent invalidation",error)&&scenes->PreviewTransformEdit(final,error),error);
        const auto changedParent=parent->GetTransform()->GetLocalPosition()+GameSproutRuntime::Vector3{4,0,0};parent->GetTransform()->SetLocalPosition(changedParent);
        Check(!scenes->CommitTransformEdit(error)&&!scenes->HasTransformEdit()&&CloseTransform(ReadTransform(*object),initial)&&CloseVector(parent->GetTransform()->GetLocalPosition(),changedParent),"Parent change committed in a stale drag coordinate space");
        Check(scenes->OpenScene("Scenes/Transaction.scene",error),error);fixture.Tick();
        parent=scenes->GetActiveScene()->FindChild("Parent");object=parent->FindChild("Editable");
        Check(scenes->BeginTransformEdit(object->GetInstanceID(),"Delete target",error)&&scenes->PreviewTransformEdit(final,error),error);
        Check(scenes->GetActiveScene()->DestroyGameObject(object),"Cannot delete active preview target");fixture.Tick();
        Check(!scenes->HasTransformEdit()&&!scenes->CommitTransformEdit(error),"Deleted target retained an active transform transaction");
        scenes->UnregisterSceneListener(&listener);
        object=scenes->GetActiveScene()->CreateGameObject("Release active edit");
        Check(scenes->BeginTransformEdit(object->GetInstanceID(),"Release",error)&&scenes->PreviewTransformEdit(final,error),error);
        fixture.Shutdown();Check(!scenes->HasTransformEdit(),"Release retained active transform edit state");
        std::cout<<"PASS real SceneManager preview, one-command Undo, cancellation, guards and lifetime\n";return 0;
    }catch(const std::exception& exception){std::cerr<<"FAIL "<<exception.what()<<'\n';return 1;}
}
