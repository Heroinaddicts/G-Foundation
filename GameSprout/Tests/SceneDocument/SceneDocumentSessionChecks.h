#pragma once

static SceneDocumentSessionRequest TemporaryDocument() {
    Json::Value document;document["Version"]=1;document["Name"]="Temporary content";
    document["NextObjectID"]=Json::UInt64(105);document["Objects"]=Json::Value(Json::arrayValue);
    for(const uint64_t id:{101,103}) {
        Json::Value node;node["ID"]=Json::UInt64(id);node["Name"]=id==101?"Content root":"Content child";
        node["ParentID"]=Json::UInt64(id==101?0:101);node["Components"]=Json::Value(Json::arrayValue);
        Json::Value transform;transform["ID"]=Json::UInt64(id+1);transform["Type"]="GameSproutRuntime.Transform";
        node["Components"].append(transform);document["Objects"].append(node);
    }
    return {AssetArtifactIO::JsonText(document),"Temporary authoring",101};
}
class DocumentFrameProbe final:public IRuntimeFrameListener {
public:
    explicit DocumentFrameProbe(ISceneManager& scenes):Scenes(scenes) {}
    ISceneManager& Scenes;bool Rejected=false;
    void OnRuntimeFrame(const RuntimeFrameEvent&) override {
        std::string error;
        Rejected=!Scenes.EndDocumentSession(error)&&!Scenes.EditSuspendedDocument([](std::string&){return true;},error);
    }
};
static void CheckDocumentSession(SceneManager& manager,GameSproutEditor::GameSproutModuleApi& source,
                                 const QDir& root,GameSproutRuntime::IGameSproutRuntime* runtime) {
    std::string error,path;
    Check(manager.CreateSceneAsset("Scenes","Document session main",path,error)&&manager.OpenScene(path,error),error);
    auto* main=manager.GetActiveScene();auto* author=main->CreateGameObject("Main retained");
    auto* authorTransform=author->GetTransform();const auto authorID=author->GetInstanceID();
    const auto fileID=manager.GetObjectFileID(authorID);Check(manager.SaveScene(error),error);
    const auto disk=Read(root.filePath("Assets/"+QString::fromStdString(path)));
    const auto rename=[](const std::string& value){return [value](GameSproutRuntime::IGameObject& object,std::string&){object.SetName(value);return true;};};
    Check(manager.EditObject(authorID,"Unsaved main rename",rename("Main pending"),rename("Main retained"),error),error);
    PlaySelectionRegistry registry(*source._EditorApi);GameSproutEditor::GameSproutModuleApi api(source._Foundation,&registry);
    SelectionManager selection;PlaySelectionScope selectionScope(selection,api);
    Check(GameSproutEditor::GameSproutEditorFinal::Initialize(selection,api)&&GameSproutEditor::GameSproutEditorFinal::Launch(selection,api),"Cannot initialize document selection fixture");
    const SceneObjectSelection savedSelection{main->GetInstanceID(),authorID};selection.SetSceneObjectSelection(savedSelection);
    const auto request=TemporaryDocument();const auto count=runtime->GetSceneCount();Listener events;manager.RegisterSceneListener(&events);
    Check(manager.BeginDocumentSession(request,error),"Begin document session: "+error);
    auto* temporary=manager.GetActiveScene();auto* content=manager.GetDocumentContentRoot();const auto temporaryID=temporary->GetInstanceID();
    Check(manager.IsDocumentSessionActive()&&!manager.IsEditingSuspendedDocument()&&temporary!=main&&manager.GetMainScene()==main&&
          main->IsActiveSelf()&&!main->IsSimulationEnabled()&&temporary->IsSimulationEnabled()&&runtime->QueryGameObject(authorID)==author&&author->GetTransform()==authorTransform&&runtime->GetSceneCount()==count+1,
          "Document switch changed the main Runtime identity/active state");
    Check(content&&content!=temporary&&manager.GetObjectFileID(content->GetInstanceID())==101&&!manager.HasUnsavedChanges()&&!manager.CanUndo(),
          "Temporary document did not use a clean independent content/history context");
    Check(selection.GetSceneObjectSelection().ObjectID==0,"World switch retained a stale selection");
    selection.SetSceneObjectSelection({temporaryID,content->GetInstanceID()});
    auto* host=source._EditorApi->GetGameSproutModule<IRuntimeHost>("RuntimeHost");DocumentFrameProbe frameProbe(manager);
    Check(host&&host->RegisterFrameListener(&frameProbe),"Cannot register session safety probe");
    GameSproutEditor::GameSproutEditorFinal::Update(*host,source);host->UnregisterFrameListener(&frameProbe);
    Check(frameProbe.Rejected&&manager.GetActiveScene()==temporary,"A frame callback replaced/switched document contexts");
    Check(!manager.BeginDocumentSession(request,error)&&!manager.BeginPlaySession(error)&&!manager.OpenScene(path,error)&&
          !manager.SaveScene(error)&&!manager.SaveSceneAs("Scenes/Invalid temporary.scene",error)&&Read(root.filePath("Assets/"+QString::fromStdString(path)))==disk,
          "Temporary document escaped through scene/play persistence entry points");
    Check(!manager.DeleteObject(content->GetInstanceID(),error)&&runtime->QueryGameObject(content->GetInstanceID())==content,"Deleted protected content root");
    Json::Value inserted;Check(AssetArtifactIO::Parse(request.DocumentJson,inserted),"Cannot parse insertion fixture");
    inserted["Objects"].resize(1);inserted["Objects"][0]["Name"]="Inserted under content";
    SceneDocumentEditRequest insertion;insertion.DocumentJson=AssetArtifactIO::JsonText(inserted);insertion.RemapDocumentIDs=true;
    SceneSubtreeDocument insertionResult;
    Check(manager.EditObjectDocument(insertion,insertionResult,error)&&runtime->QueryGameObject(insertionResult.RootObjectID)->Parent()==content,
          "Default document insertion escaped its content root: "+error);
    Check(manager.Undo(error)&&content->GetChildCount()==1,"Cannot undo temporary document insertion");
    Check(manager.EditObject(content->GetInstanceID(),"Temporary rename",rename("Changed content"),rename("Content root"),error)&&manager.HasUnsavedChanges()&&manager.CanUndo(),error);
    Check(manager.MarkDocumentSessionSaved(error)&&!manager.HasUnsavedChanges(),"Cannot mark a pathless document clean: "+error);
    Check(manager.Undo(error)&&content->GetName()=="Content root"&&manager.HasUnsavedChanges(),"Temporary undo used main history");
    Check(manager.MarkDocumentSessionSaved(error),error);
    SceneSubtreeDocument snapshot;Check(manager.CaptureObjectDocument(content->GetInstanceID(),snapshot,error),error);
    Json::Value moved;Check(AssetArtifactIO::Parse(snapshot.DocumentJson,moved),"Cannot parse content snapshot");moved["Objects"][0]["ParentID"]=Json::UInt64(103);
    SceneDocumentEditRequest edit;edit.RootObjectID=content->GetInstanceID();edit.ParentObjectID=content->GetChild(0)->GetInstanceID();
    edit.DocumentJson=AssetArtifactIO::JsonText(moved);SceneSubtreeDocument ignored;
    Check(!manager.EditObjectDocument(edit,ignored,error)&&content->Parent()==temporary,"Reparented protected root through document edit");
    const auto sceneEvents=events.Active,stateEvents=events.State;
    Check(manager.EditSuspendedDocument([&](std::string& reason) {
        Check(manager.IsEditingSuspendedDocument()&&manager.GetActiveScene()==main&&manager.GetDocumentContentRoot()==main&&manager.QueryObjectByFileID(fileID)==author,
              "Suspended callback does not address the original main document");
        Check(!manager.EndDocumentSession(reason)&&!manager.BeginDocumentSession(request,reason),"Background callback changed its owner document session");
        return manager.EditObject(authorID,"Background source update",rename("Main background"),rename("Main pending"),reason);
    },error),"Background edit failed: "+error);
    Check(manager.GetActiveScene()==temporary&&manager.GetDocumentContentRoot()==content&&content->GetName()=="Content root"&&
          manager.CanRedo()&&!manager.HasUnsavedChanges()&&events.Active==sceneEvents&&events.State==stateEvents&&
          selection.GetSceneObjectSelection()==SceneObjectSelection{temporaryID,content->GetInstanceID()},"Background update leaked main context/notifications into the temporary document");
    Check(!manager.EditSuspendedDocument([](std::string&)->bool{throw std::runtime_error("Expected failure");},error)&&manager.GetActiveScene()==temporary&&!manager.IsEditingSuspendedDocument(),
          "Throwing background callback did not restore the temporary context");
    Check(manager.EndDocumentSession(error),"End document session: "+error);
    Check(!manager.IsDocumentSessionActive()&&manager.GetActiveScene()==main&&manager.GetMainScene()==main&&manager.GetDocumentContentRoot()==main&&
          runtime->QueryGameObject(authorID)==author&&author->GetTransform()==authorTransform&&manager.QueryObjectByFileID(fileID)==author&&
          author->GetName()=="Main background"&&main->IsSimulationEnabled()&&manager.GetActiveScenePath()==path&&manager.HasUnsavedChanges()&&runtime->GetSceneCount()==count,
          "Document close rebuilt or lost the original main authoring context");
    selection.SetSceneObjectSelection(savedSelection);Check(selection.GetSceneObjectSelection()==savedSelection,"Original identity could not restore main selection");
    Check(manager.Undo(error)&&author->GetName()=="Main pending"&&manager.Undo(error)&&author->GetName()=="Main retained", "Main undo history did not survive the document session");
    auto invalid=request;invalid.ContentRootFileID=999;
    Check(!manager.BeginDocumentSession(invalid,error)&&manager.GetActiveScene()==main&&runtime->GetSceneCount()==count,"Invalid candidate replaced/leaked the main document");
    main->SetSimulationEnabled(false);
    Check(manager.BeginDocumentSession(request,error)&&!main->IsSimulationEnabled()&&manager.EndDocumentSession(error)&&!main->IsSimulationEnabled(),
          "Session did not preserve an already-disabled main simulation: "+error);
    main->SetSimulationEnabled(true);
    manager.UnregisterSceneListener(&events);
}
