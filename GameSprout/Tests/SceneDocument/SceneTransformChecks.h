#pragma once

static SceneTransformState TransformState(GameSproutRuntime::IGameObject& object) {
    auto* transform=object.GetTransform();
    return {transform->GetLocalPosition(),transform->GetLocalRotation(),transform->GetLocalScale()};
}
static bool ExactTransform(const SceneTransformState& a,const SceneTransformState& b) {
    return a.Position.X==b.Position.X&&a.Position.Y==b.Position.Y&&a.Position.Z==b.Position.Z&&
        a.Rotation.X==b.Rotation.X&&a.Rotation.Y==b.Rotation.Y&&a.Rotation.Z==b.Rotation.Z&&a.Rotation.W==b.Rotation.W&&
        a.Scale.X==b.Scale.X&&a.Scale.Y==b.Scale.Y&&a.Scale.Z==b.Scale.Z;
}
class TransformNotificationGuard final : public ISceneListener {
public:
    SceneManager& Manager;
    int Calls=0;
    bool Reentered=false;
    explicit TransformNotificationGuard(SceneManager& manager):Manager(manager) {}
    void OnActiveSceneChanged() override {}
    void OnSceneStateChanged() override {
        ++Calls;
        if(Manager.HasTransformEdit()) {std::string error;Reentered|=Manager.CancelTransformEdit(error)||Manager.CommitTransformEdit(error);}
    }
};
static void CheckTransformTransactions(SceneManager& manager,GameSproutEditor::GameSproutModuleApi& api,const QDir& root) {
    std::string path,error;
    Check(manager.CreateSceneAsset("Scenes","Transactions",path,error)&&manager.OpenScene(path,error),error);
    auto* parent=manager.GetActiveScene()->CreateGameObject("Transform parent");parent->GetTransform()->SetLocalScale({2,1,3});
    Check(parent->GetTransform()->SetLocalRotation({.29f,-.17f,.43f,.71f}),"Parent quaternion rejected");
    auto* object=manager.GetActiveScene()->CreateGameObject("Transform object",parent);
    object->GetTransform()->SetLocalPosition({1.25f,-.75f,2.5f});object->GetTransform()->SetLocalScale({1.2f,.7f,-1.3f});
    Check(object->GetTransform()->SetLocalRotation({.13f,.27f,.39f,.81f}),"Initial quaternion rejected");
    Check(manager.SaveScene(error),error);
    const auto file=root.filePath("Assets/"+QString::fromStdString(path));const auto bytes=Read(file);
    // 不仅比较近似旋转：反序列化不得再次归一化已有float四元数并改变保存文本。
    Check(manager.OpenScene(path,error)&&manager.SaveScene(error)&&Read(file)==bytes&&!manager.HasUnsavedChanges(),
        "Nontrivial parent/child quaternion Save/Open/Save changed serialized bytes: "+error);
    parent=manager.GetActiveScene()->FindChild("Transform parent");object=parent?parent->FindChild("Transform object"):nullptr;
    Check(object,"Quaternion persistence fixture lost parent/child hierarchy");
    const auto original=TransformState(*object);const auto id=object->GetInstanceID();
    Check(manager.EditObject(id,"Rename",[](auto& node,std::string&){node.SetName("Renamed");return true;},
        [](auto& node,std::string&){node.SetName("Transform object");return true;},error)&&manager.Undo(error)&&manager.CanRedo(),error);
    TransformNotificationGuard guard(manager);Check(manager.RegisterSceneListener(&guard),"Cannot observe preview");
    Check(manager.BeginTransformEdit(id,"Move/Rotate/Scale",error),error);
    auto changed=original;changed.Position={2,3,4};changed.Rotation={.12f,.34f,.56f,.78f};changed.Scale={2,3,-4};
    Check(manager.PreviewTransformEdit(changed,error),error);const auto last=TransformState(*object);
    Check(guard.Calls>0&&!guard.Reentered&&manager.HasTransformEdit(),"Preview notification allowed reentrant completion");
    auto invalid=changed;invalid.Position.X=std::numeric_limits<float>::max();
    Check(!manager.PreviewTransformEdit(invalid,error)&&ExactTransform(TransformState(*object),last),"World overflow preview was not atomically rejected");
    invalid=changed;invalid.Rotation={0,0,0,0};
    Check(!manager.PreviewTransformEdit(invalid,error)&&ExactTransform(TransformState(*object),last),"Invalid quaternion altered previous preview");
    Check(!manager.SaveScene(error)&&!manager.Undo(error)&&!manager.Redo(error)&&!manager.OpenScene(path,error),"Transaction failed to guard scene operations");
    Check(manager.CancelTransformEdit(error)&&ExactTransform(TransformState(*object),original)&&manager.CanRedo(),"Cancel changed quaternion/history");
    Check(manager.SaveScene(error)&&Read(file)==bytes&&!manager.HasUnsavedChanges(),"Cancel introduced snapshot quaternion drift");
    Check(manager.BeginTransformEdit(id,"Equivalent quaternion",error),error);
    auto equivalent=original;equivalent.Rotation={original.Rotation.X*7,original.Rotation.Y*7,original.Rotation.Z*7,original.Rotation.W*7};
    Check(manager.PreviewTransformEdit(equivalent,error)&&manager.CommitTransformEdit(error)&&ExactTransform(TransformState(*object),original)&&
        manager.CanRedo()&&!manager.CanUndo()&&!manager.HasUnsavedChanges(),"Equivalent no-op drag lost Redo or dirtied the scene");
    Check(manager.BeginTransformEdit(id,"Committed drag",error)&&manager.PreviewTransformEdit(changed,error)&&manager.CommitTransformEdit(error),error);
    Check(manager.CanUndo()&&!manager.CanRedo()&&manager.Undo(error)&&ExactTransform(TransformState(*object),original)&&!manager.CanUndo(),"One drag did not produce exactly one Undo");
    Check(manager.Redo(error)&&ExactTransform(TransformState(*object),last)&&manager.Undo(error),"Committed drag Redo did not restore exact final values");
    Check(manager.BeginTransformEdit(id,"Parent changed",error)&&manager.PreviewTransformEdit(changed,error),error);
    parent->GetTransform()->SetLocalPosition({4,0,0});GameSproutEditor::GameSproutEditorFinal::Update(manager,api);
    Check(!manager.HasTransformEdit()&&ExactTransform(TransformState(*object),original)&&parent->GetTransform()->GetLocalPosition().X==4&&manager.CanRedo(),"Parent change did not cancel child-only preview");
    parent->GetTransform()->SetLocalPosition({0,0,0});Check(manager.SaveScene(error)&&Read(file)==bytes,error);
    manager.UnregisterSceneListener(&guard);
}
