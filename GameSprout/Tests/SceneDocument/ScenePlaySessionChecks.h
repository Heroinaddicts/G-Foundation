#pragma once

static void CheckPlaySceneSession(SceneManager& manager,GameSproutEditor::GameSproutModuleApi& api,
                                  const QDir& root,GameSproutRuntime::IGameSproutRuntime* runtime) {
    std::string error,path;
    Check(manager.CreateSceneAsset("Scenes","PlaySession",path,error)&&manager.OpenScene(path,error),error);
    const QByteArray disk=Read(root.filePath("Assets/Scenes/PlaySession.scene"));
    auto* scene=manager.GetActiveScene();auto* camera=scene->GetMainCamera()->GetGameObject();
    const auto cameraFileID=manager.GetObjectFileID(camera->GetInstanceID());
    const auto rename=[](const std::string& name) {
        return [name](GameSproutRuntime::IGameObject& object,std::string&) {object.SetName(name);return true;};
    };
    Check(manager.EditObject(camera->GetInstanceID(),"Before Play A",rename("Unsaved A"),rename(camera->GetName()),error),error);
    Check(manager.EditObject(camera->GetInstanceID(),"Before Play B",rename("Unsaved B"),rename("Unsaved A"),error),error);
    Check(manager.Undo(error)&&manager.CanUndo()&&manager.CanRedo(),"Cannot prepare author Undo/Redo branch");
    auto* unsaved=scene->CreateGameObject("Never saved");unsaved->GetTransform()->SetLocalPosition({3,4,5});
    const auto unsavedFileID=manager.GetObjectFileID(unsaved->GetInstanceID());
    auto* authorExtra=runtime->CreateScene("Author extra");Check(authorExtra&&authorExtra->CreateGameObject("Extra kept"),"Cannot create second author scene");
    const auto oldID=scene->GetInstanceID();
    const auto authorExtraID=authorExtra->GetInstanceID();
    Check(manager.HasUnsavedChanges(),"Author changes must be dirty before Play");
    // Invalid author data and callback reentry must reject without publishing a session.
    scene->SetName("");
    Check(!manager.BeginPlaySession(error)&&!manager.IsPlaySessionActive()&&manager.GetActiveScene()==scene&&runtime->GetSceneCount()==2,
          "Failed scene capture changed author state");
    scene->SetName("PlaySession");
    bool guarded=false;
    scene->ForeachChild([&](GameSproutRuntime::IGameObject*) {
        if(guarded)return;guarded=true;
        Check(!manager.BeginPlaySession(error)&&!manager.IsPlaySessionActive(),"Runtime callback entered Play scene session");
    });
    Check(guarded,"Play callback guard was not exercised");
    Check(runtime->SetScriptExecutionEnabled(true,error),error);
    Check(!manager.BeginPlaySession(error),"Play scene session accepted executing script world");
    Check(runtime->SetScriptExecutionEnabled(false,error),error);
    Check(manager.BeginPlaySession(error)&&manager.IsPlaySessionActive(),error);
    Check(manager.GetActiveScene()->GetInstanceID()!=oldID&&runtime->GetSceneCount()==2,"Play did not replace all author scenes");
    Check(!runtime->QueryGameObject(oldID)&&!runtime->QueryGameObject(authorExtraID),"Parked author scenes were visible during Play");
    camera=manager.QueryObjectByFileID(cameraFileID);unsaved=manager.QueryObjectByFileID(unsavedFileID);
    Check(camera&&camera->GetName()=="Unsaved A"&&unsaved&&unsaved->GetTransform()->GetLocalPosition().X==3,
          "Play did not start from unsaved author memory");
    Check(!manager.CanUndo()&&!manager.CanRedo()&&!manager.Undo(error)&&!manager.Redo(error)&&
          !manager.SaveScene(error)&&!manager.SaveSceneAs("Scenes/RuntimeLeak.scene",error)&&
          !manager.OpenScene(path,error)&&!manager.AddBuiltinComponent(camera->GetInstanceID(),"GameSproutRuntime.Mesh",error)&&
          !manager.DeleteObject(camera->GetInstanceID(),error),"Play accepted source scene mutation/history/save");
    Check(!QFileInfo::exists(root.filePath("Assets/Scenes/RuntimeLeak.scene")),"Play saved runtime source data");
    Listener listener;Check(manager.RegisterSceneListener(&listener),"Cannot observe Play scene state");
    for(int index=0;index<5;++index) {
        camera->GetTransform()->SetLocalPosition({float(index),0,0});
        runtime->Update(0);GameSproutEditor::GameSproutEditorFinal::Update(manager,api);
    }
    Check(listener.State==0&&manager.HasUnsavedChanges(),"Runtime transforms triggered author dirty-state serialization/events");
    auto* playScene=manager.GetActiveScene();
    Check(playScene->DestroyGameObject(unsaved),"Cannot delete authored node during Play");
    auto* runtimeOnly=playScene->CreateGameObject("Runtime only");Check(runtimeOnly,"Cannot create runtime node");
    const auto runtimeOnlyID=runtimeOnly->GetInstanceID();
    Check(manager.GetObjectFileID(runtimeOnlyID)==0,"Runtime node received author file identity");
    auto* spawned=runtime->CreateScene("Runtime spawned");Check(spawned&&spawned->CreateGameObject("Spawned child"),"Cannot create runtime scene");
    Check(runtime->SetScriptExecutionEnabled(true,error),error);
    Check(!manager.EndPlaySession(error)&&manager.IsPlaySessionActive(),"Stop restored scene before script release");
    Check(runtime->SetScriptExecutionEnabled(false,error),error);
    // Even destruction of the entire current running scene must not erase the author backup.
    Check(runtime->DestroyScene(playScene),"Cannot destroy active Play world");runtime->Update(0);
    Check(!manager.GetActiveScene()&&manager.IsPlaySessionActive()&&manager.HasUnsavedChanges(),"Play scene deletion lost author session");
    Check(manager.EndPlaySession(error)&&!manager.IsPlaySessionActive(),error);
    Check(runtime->GetSceneCount()==2&&!runtime->QueryGameObject(runtimeOnlyID)&&manager.GetActiveScene()==scene&&
          runtime->GetScene(0)==scene&&runtime->GetScene(1)==authorExtra,"Stop lost author identities/order or retained runtime-created worlds");
    camera=manager.QueryObjectByFileID(cameraFileID);unsaved=manager.QueryObjectByFileID(unsavedFileID);
    Check(camera&&camera->GetName()=="Unsaved A"&&unsaved&&unsaved->GetTransform()->GetLocalPosition().X==3,
          "Stop lost author nodes, field values, or stable file identities");
    Check(manager.CanUndo()&&manager.CanRedo()&&manager.HasUnsavedChanges()&&manager.GetActiveScenePath()==path,
          "Stop lost author path, dirty state, Undo or Redo");
    Check(manager.Redo(error)&&camera->GetName()=="Unsaved B"&&manager.Undo(error)&&camera->GetName()=="Unsaved A",error);
    Check(Read(root.filePath("Assets/Scenes/PlaySession.scene"))==disk,"Play/Stop overwrote source .scene");
    manager.UnregisterSceneListener(&listener);
    // A clean author scene remains clean despite arbitrary runtime mutations.
    Check(manager.SaveScene(error)&&!manager.HasUnsavedChanges(),error);
    Check(manager.BeginPlaySession(error),error);
    manager.GetActiveScene()->SetName(""); // 不可序列化的临时运行状态同样必须能够停止。
    runtime->Update(0);GameSproutEditor::GameSproutEditorFinal::Update(manager,api);
    Check(!manager.HasUnsavedChanges(),"Runtime changes dirtied clean author document");
    Check(manager.EndPlaySession(error)&&!manager.HasUnsavedChanges(),error);
    // Release fixture's additional author scene; the primary scene belongs to SceneManager.
    for(std::size_t index=runtime->GetSceneCount();index>0;--index) {
        auto* extra=runtime->GetScene(index-1);if(extra!=manager.GetActiveScene())Check(runtime->DestroyScene(extra),"Cannot release extra author scene");
    }
}

static void CheckLargePlaySnapshot(SceneManager& manager,const QDir& root) {
    std::string error,path;
    Check(manager.CreateSceneAsset("Scenes","LargePlaySnapshot",path,error)&&manager.OpenScene(path,error),error);
    auto* author=manager.GetActiveScene();
    for(std::size_t index=0;index<2050;++index)
        Check(author->CreateGameObject("Saved "+std::to_string(index)),"Cannot create Play snapshot fixture");
    Check(manager.SaveScene(error),error);
    const auto source=root.filePath("Assets/"+QString::fromStdString(path));
    const auto before=Read(source);
    auto* removed=author->GetChild(17);
    const auto removedFileID=manager.GetObjectFileID(removed->GetInstanceID());
    auto* moved=author->GetChild(1030);
    Check(author->DestroyGameObject(removed)&&moved->SetSiblingIndex(1),"Cannot edit large Play snapshot");
    moved->SetName("Unsaved move");
    auto* added=author->CreateGameObject("Unsaved addition");
    Check(added&&manager.HasUnsavedChanges(),"Cannot add unsaved Play object");
    const auto count=author->GetChildCount();
    std::vector<std::uint64_t> order;
    order.reserve(count);
    for(std::size_t index=0;index<count;++index)
        order.push_back(manager.GetObjectFileID(author->GetChild(index)->GetInstanceID()));
    Check(manager.BeginPlaySession(error),error);
    auto* running=manager.GetActiveScene();
    Check(running!=author&&running->GetChildCount()==count&&!manager.QueryObjectByFileID(removedFileID),
          "Play snapshot lost rows or restored a deleted row");
    for(std::size_t index=0;index<count;++index)
        Check(order[index]&&manager.GetObjectFileID(running->GetChild(index)->GetInstanceID())==order[index],
              "Play snapshot changed author sibling order or stable identity");
    Check(running->GetChild(1)->GetName()=="Unsaved move"&&running->GetChild(count-1)->GetName()=="Unsaved addition",
          "Play snapshot lost unsaved row contents");
    running->GetChild(1)->SetName("Runtime-only change");
    Check(manager.EndPlaySession(error)&&manager.GetActiveScene()==author&&author->GetChild(1)==moved&&
          moved->GetName()=="Unsaved move"&&author->GetChild(count-1)==added&&Read(source)==before,
          "Stop lost original author objects or wrote Play edits to disk");
}
