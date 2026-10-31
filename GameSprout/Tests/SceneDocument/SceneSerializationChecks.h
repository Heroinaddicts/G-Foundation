#pragma once

static const Json::Value& FileNode(const Json::Value& document,const std::string& name) {
    for (const auto& node:document["Objects"]) if (node["Name"].asString()==name) return node;
    throw std::runtime_error("Missing serialized node: "+name);
}
static void CheckSerialization(GameSproutRuntime::IGameSproutRuntime& runtime,const Json::Value& source) {
    GameSproutRuntime::IScene* scene=nullptr;
    SceneDocument::ObjectIDs ids;
    std::string error;
    Check(SceneDocument::Instantiate(source,runtime,scene,ids,error),error);
    auto* parent=scene->FindChild("Parent");
    auto* first=parent->FindChild("First");
    scene->SetName("Round trip scene");scene->SetActive(false);first->SetActive(false);
    const GameSproutRuntime::ResourceReference mesh{"0123456789abcdef0123456789abcdef",91};
    const GameSproutRuntime::ResourceReference material{"ffffffffffffffffffffffffffffffff",202};
    first->GetComponent<GameSproutRuntime::IMeshComponent>()->SetMesh(mesh);
    auto* renderer=first->GetComponent<GameSproutRuntime::IMeshRendererComponent>();
    Check(renderer->SetMaterials({material,{},material}),"Cannot configure material slot round trip");
    renderer->SetVisible(false);
    first->GetComponent<GameSproutRuntime::IImageComponent>()->SetUVRect({{0.2f,0.3f},{0.4f,0.5f}});
    auto* added=scene->CreateGameObject("Added",parent);
    Json::Value saved;
    Check(SceneDocument::Serialize(*scene,ids,saved,error),error);
    Check(saved["Name"].asString()=="Round trip scene" && !saved["RootActive"].asBool(),"Root name/active not serialized");
    Check(FileNode(saved,"First")["ID"].asUInt64()==30 && FileNode(saved,"Parent")["ID"].asUInt64()==20 &&
          FileNode(saved,"Added")["ID"].asUInt64()==41,"Loaded IDs changed or new ID was not allocated monotonically");
    Check(saved["MainCameraID"].asUInt64()==30 && saved["NextObjectID"].asUInt64()==42,"Main camera/File ID high water mark mismatch");
    Check(FileNode(saved,"First")["Components"][4]["Mesh"]["LocalID"].asUInt64()==91,"Mesh resource identity lost");
    Check(FileNode(saved,"First")["Components"][5]["Materials"][1].isNull(),"Unbound slot must remain null");
    Check(!FileNode(saved,"First")["Components"][4].isMember("Bounds"),"Derived mesh data was written to source");
    GameSproutRuntime::IScene* loaded=nullptr;
    SceneDocument::ObjectIDs loadedIDs;
    Check(SceneDocument::Instantiate(saved,runtime,loaded,loadedIDs,error),error);
    auto* loadedParent=loaded->FindChild("Parent");
    auto* loadedFirst=loadedParent->FindChild("First");
    Check(!loaded->IsActiveSelf() && !loadedFirst->IsActiveSelf() && loadedParent->GetChildCount()==3 &&
          loadedParent->GetChild(0)->GetName()=="First" && loadedParent->GetChild(2)->GetName()=="Added","Active state/order round trip failed");
    Check(loadedFirst->GetComponent<GameSproutRuntime::IMeshComponent>()->GetMesh()==mesh,"Mesh reference round trip failed");
    Check(loadedFirst->GetComponent<GameSproutRuntime::IMeshRendererComponent>()->GetMaterialCount()==3 &&
          !loadedFirst->GetComponent<GameSproutRuntime::IMeshRendererComponent>()->IsVisible() &&
          loadedFirst->GetComponent<GameSproutRuntime::IMeshRendererComponent>()->GetMaterial(2)==material,"Material slot round trip failed");
    Json::Value savedAgain;
    Check(SceneDocument::Serialize(*loaded,loadedIDs,savedAgain,error),error);
    Check(savedAgain==saved,"Reopen and save changed canonical scene document");
    Check(scene->DestroyGameObject(added),"Cannot delete highest File ID object");
    Check(SceneDocument::Serialize(*scene,ids,saved,error),error);
    Check(SceneDocument::Instantiate(saved,runtime,loaded,loadedIDs,error),error);
    loaded->CreateGameObject("After deletion");
    Check(SceneDocument::Serialize(*loaded,loadedIDs,savedAgain,error),error);
    Check(FileNode(savedAgain,"After deletion")["ID"].asUInt64()==42,"Deleted File ID was reused after reopening");
    const auto oldIDs=loadedIDs.RuntimeToFile;const auto oldNext=loadedIDs.Next;const auto oldDocument=savedAgain;
    loaded->SetName("");
    Check(!SceneDocument::Serialize(*loaded,loadedIDs,savedAgain,error) && savedAgain==oldDocument &&
          loadedIDs.RuntimeToFile==oldIDs && loadedIDs.Next==oldNext,"Serialization failure mutated output/IDs");
    loaded->SetName("Root camera");
    auto* camera=loaded->AddComponent<GameSproutRuntime::ICameraComponent>();
    loaded->AddComponent<GameSproutRuntime::IImageComponent>();
    Check(loaded->SetMainCamera(camera),"Cannot assign root camera");
    Check(SceneDocument::Serialize(*loaded,loadedIDs,savedAgain,error),error);
    Check(savedAgain["RootMainCamera"].asBool() && savedAgain["MainCameraID"].asUInt64()==0,"Root camera was confused with no camera");
    Check(SceneDocument::Instantiate(savedAgain,runtime,scene,ids,error),error);
    Check(scene->GetComponent<GameSproutRuntime::IImageComponent>() && scene->GetMainCamera()==scene->GetComponent<GameSproutRuntime::ICameraComponent>(),"Root components/camera round trip failed");
    auto bad=savedAgain;bad["NextObjectID"]=Json::UInt64(30);
    const auto before=runtime.GetSceneCount();auto* unchanged=scene;
    Check(!SceneDocument::Instantiate(bad,runtime,scene,ids,error) && scene==unchanged && runtime.GetSceneCount()==before,"Invalid NextObjectID accepted/leaked");
    bad=savedAgain;bad["RootActive"]=1;
    Check(!SceneDocument::Validate(bad,error),"Nonbool RootActive accepted");
    bad=savedAgain;bad["RootComponents"][0]["Position"]=Vector({1,0,0});
    Check(!SceneDocument::Validate(bad,error),"Nonidentity root transform accepted");
}
