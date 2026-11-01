#pragma once

static void CheckSceneDeletion(SceneManager& manager,GameSproutEditor::GameSproutModuleApi& api,const QDir& root,GameSproutRuntime::IGameSproutRuntime* runtime) {
    std::string path,error;
    Check(manager.CreateSceneAsset("Scenes","Deletion",path,error)&&manager.OpenScene(path,error),error);
    auto* scene=manager.GetActiveScene();const auto sceneID=scene->GetInstanceID();
    auto* parent=scene->CreateGameObject("Deletion parent");const auto parentID=parent->GetInstanceID();
    parent->GetTransform()->SetLocalPosition({2,-1,3});parent->GetTransform()->SetLocalScale({2,1,3});
    Check(parent->GetTransform()->SetLocalRotation({.29f,-.17f,.43f,.71f}),"Cannot rotate deletion parent");
    auto* first=scene->CreateGameObject("First sibling",parent);
    auto* target=scene->CreateGameObject("Victim",parent);
    auto* last=scene->CreateGameObject("Last sibling",parent);
    const auto firstID=first->GetInstanceID(),lastID=last->GetInstanceID(),oldTargetID=target->GetInstanceID();
    target->GetTransform()->SetLocalPosition({1.25f,-.75f,2.5f});target->GetTransform()->SetLocalScale({1.2f,.7f,-1.3f});
    Check(target->GetTransform()->SetLocalRotation({.13f,.27f,.39f,.81f}),"Cannot rotate deletion target");
    target->AddComponent<GameSproutRuntime::IRectTransformComponent>()->SetSizeDelta({321,123});
    auto* image=target->AddComponent<GameSproutRuntime::IImageComponent>();
    image->SetTexture({"0123456789abcdef0123456789abcdef",9});image->SetColor({.2f,.4f,.6f,.8f});
    target->AddComponent<GameSproutRuntime::ICanvasComponent>()->SetPixelsPerUnit(77);
    target->AddComponent<GameSproutRuntime::IMeshComponent>()->SetMesh({"0123456789abcdef0123456789abcdef",10});
    auto* renderer=target->AddComponent<GameSproutRuntime::IMeshRendererComponent>();renderer->SetVisible(false);
    Check(renderer->SetMaterials({{"0123456789abcdef0123456789abcdef",11},{},{"0123456789abcdef0123456789abcdef",12}}),"Cannot set deletion materials");
    target->AddComponent<GameSproutRuntime::ICameraComponent>();target->SetActive(false);
    auto* child=scene->CreateGameObject("Child camera",target);const auto oldChildID=child->GetInstanceID();
    auto* grandchild=scene->CreateGameObject("Grandchild",child);const auto oldGrandchildID=grandchild->GetInstanceID();
    grandchild->AddComponent<GameSproutRuntime::IImageComponent>()->SetColor({.1f,.3f,.5f,.7f});
    Check(scene->SetMainCamera(child->AddComponent<GameSproutRuntime::ICameraComponent>()),"Cannot select deleted subtree camera");
    Check(manager.SaveScene(error),error);
    const auto targetFile=manager.GetObjectFileID(oldTargetID),childFile=manager.GetObjectFileID(oldChildID),grandchildFile=manager.GetObjectFileID(oldGrandchildID);
    Check(targetFile&&childFile&&grandchildFile&&manager.QueryObjectByFileID(0)==scene,"Stable deletion identities unavailable");
    Check(!manager.DeleteObject(sceneID,error)&&manager.GetActiveScene()==scene,"Delete accepted scene root");
    auto* foreign=runtime->CreateScene("Foreign scene");auto* foreignNode=foreign->CreateGameObject("Foreign node");
    Check(!manager.DeleteObject(foreignNode->GetInstanceID(),error)&&runtime->DestroyScene(foreign),"Delete accepted another scene's object");
    Check(manager.EditObject(oldTargetID,"Rename victim",[](auto& object,std::string&){object.SetName("Edited victim");return true;},
        [](auto& object,std::string&){object.SetName("Victim");return true;},error),error);
    const auto before=TransformState(*child);auto after=before;after.Position={3,2,-1};after.Rotation={.12f,.34f,.56f,.78f};after.Scale={2,.8f,-1.7f};
    Check(manager.BeginTransformEdit(oldChildID,"Transform child",error)&&manager.PreviewTransformEdit(after,error)&&manager.CommitTransformEdit(error),error);
    const auto transformed=TransformState(*child);
    Check(manager.SaveScene(error),error);const auto filename=root.filePath("Assets/"+QString::fromStdString(path));const auto snapshot=Read(filename);
    Check(manager.DeleteObject(oldTargetID,error),error);
    Check(!manager.QueryObjectByFileID(targetFile)&&!manager.QueryObjectByFileID(childFile)&&!manager.QueryObjectByFileID(grandchildFile)&&
        !scene->GetMainCamera()&&parent->GetChildCount()==2&&parent->GetChild(0)==first&&parent->GetChild(1)==last,
        "Delete left subtree, main camera or wrong sibling order");
    Check(manager.SaveScene(error),error);
    const auto deletedDocument=Document(filename);
    for(const auto& node:deletedDocument["Objects"])Check(node["ID"].asUInt64()!=targetFile&&node["ID"].asUInt64()!=childFile&&
        node["ID"].asUInt64()!=grandchildFile,"Deleted node was saved into the scene");
    Check(manager.Undo(error),error);
    target=manager.QueryObjectByFileID(targetFile);child=manager.QueryObjectByFileID(childFile);grandchild=manager.QueryObjectByFileID(grandchildFile);
    Check(target&&child&&grandchild&&target->GetInstanceID()!=oldTargetID&&child->GetInstanceID()!=oldChildID&&grandchild->GetInstanceID()!=oldGrandchildID,
        "Undo deletion did not recreate stable-file identities with new Runtime IDs");
    Check(manager.GetActiveScene()==scene&&scene->GetInstanceID()==sceneID&&parent->GetInstanceID()==parentID&&
        first->GetInstanceID()==firstID&&last->GetInstanceID()==lastID&&parent->GetChild(0)==first&&parent->GetChild(1)==target&&parent->GetChild(2)==last&&
        child->Parent()==target&&grandchild->Parent()==child&&scene->GetMainCamera()==child->GetComponent<GameSproutRuntime::ICameraComponent>(),
        "Undo deletion replaced untouched objects, hierarchy, order or main camera");
    Check(target->GetComponentCount()==7&&!target->IsActiveSelf()&&manager.SaveScene(error)&&Read(filename)==snapshot,
        "Undo deletion did not restore exact full component/settings/resource/FileID data");
    Check(manager.Undo(error)&&ExactTransform(TransformState(*child),before),"Old transform Undo did not resolve restored child's FileID");
    Check(manager.Undo(error)&&target->GetName()=="Victim"&&!manager.CanUndo(),"Old attribute Undo did not resolve restored parent's FileID");
    Check(manager.Redo(error)&&target->GetName()=="Edited victim"&&manager.Redo(error)&&ExactTransform(TransformState(*child),transformed)&&
        manager.Redo(error)&&!manager.QueryObjectByFileID(targetFile)&&!manager.CanRedo(),"Redo chain failed across recreated Runtime IDs");
    Check(manager.Undo(error),error);target=manager.QueryObjectByFileID(targetFile);
    Check(manager.BeginTransformEdit(target->GetInstanceID(),"Deletion guard",error)&&!manager.DeleteObject(target->GetInstanceID(),error)&&
        manager.CancelTransformEdit(error)&&manager.CanRedo(),"Delete bypassed active transform guard or truncated Redo");
    bool callbackGuard=false;
    scene->ForeachChild([&](GameSproutRuntime::IGameObject*) {callbackGuard=!manager.DeleteObject(target->GetInstanceID(),error);});
    Check(callbackGuard&&manager.QueryObjectByFileID(targetFile)==target,"Delete ran inside Runtime traversal");
    auto* external=scene->CreateGameObject("External new child",target);const auto externalID=external->GetInstanceID();
    Check(!manager.Redo(error)&&runtime->QueryGameObject(externalID)==external&&manager.QueryObjectByFileID(targetFile)==target,
        "Old delete Redo discarded an unrecorded new child");
    Check(scene->DestroyGameObject(external)&&manager.Redo(error)&&manager.Undo(error),"Valid delete could not redo after resolving external conflict");
    const auto parentFile=manager.GetObjectFileID(parent->GetInstanceID());
    target=manager.QueryObjectByFileID(targetFile);
    Check(manager.DeleteObject(target->GetInstanceID(),error)&&manager.DeleteObject(parent->GetInstanceID(),error),error);
    Check(manager.Undo(error)&&manager.QueryObjectByFileID(parentFile)&&!manager.QueryObjectByFileID(targetFile),
        "Undo parent deletion unexpectedly restored an earlier deleted child");
    Check(manager.Undo(error),error);
    auto* restoredParent=manager.QueryObjectByFileID(parentFile);target=manager.QueryObjectByFileID(targetFile);
    Check(target&&target->Parent()==restoredParent&&restoredParent->GetChild(1)==target&&scene->GetMainCamera()==
        manager.QueryObjectByFileID(childFile)->GetComponent<GameSproutRuntime::ICameraComponent>(),
        "Older subtree Undo did not resolve its recreated parent by stable FileID");
    GameSproutEditor::GameSproutEditorFinal::Update(manager,api);
}
