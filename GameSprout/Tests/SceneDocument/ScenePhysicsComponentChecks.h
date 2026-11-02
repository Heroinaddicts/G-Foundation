#pragma once

static void CheckPhysicsComponentTransactions(SceneManager& manager,GameSproutRuntime::IGameSproutRuntime* runtime) {
    std::string path,error;
    Check(manager.CreateSceneAsset("Scenes","PhysicsComponents",path,error)&&manager.OpenScene(path,error),error);
    auto* scene=manager.GetActiveScene();auto* object=scene->CreateGameObject("Physics target");const auto objectID=object->GetInstanceID();
    Check(manager.SaveScene(error),error);
    Check(manager.AddBuiltinComponent(objectID,"GameSproutRuntime.BoxCollider",error),error);
    Check(manager.AddBuiltinComponent(objectID,"GameSproutRuntime.BoxCollider",error),error);
    auto colliders=object->GetComponents<GameSproutRuntime::IBoxColliderComponent>();
    Check(colliders.size()==2,"Editor rejected repeated BoxCollider");
    const auto firstLive=colliders[0]->GetComponentInstanceID(),secondLive=colliders[1]->GetComponentInstanceID();
    const auto firstFile=manager.GetComponentFileID(objectID,firstLive),secondFile=manager.GetComponentFileID(objectID,secondLive);
    Check(firstFile&&secondFile&&firstFile!=secondFile,"Repeated Collider lacks unique stable file identity");
    const auto shape=[&manager,secondFile](GameSproutRuntime::IGameObject& owner,std::string& reason,float size) {
        auto* box=dynamic_cast<GameSproutRuntime::IBoxColliderComponent*>(manager.QueryComponentByFileID(owner.GetInstanceID(),secondFile));
        if(!box){reason="Collider file identity missing";return false;}return box->SetShape({{size,size,size}});
    };
    Check(manager.EditObject(objectID,"Second collider size",[shape](auto& owner,auto& reason){return shape(owner,reason,2);},
        [shape](auto& owner,auto& reason){return shape(owner,reason,1);},error),error);
    Check(manager.RemoveComponentInstance(objectID,secondLive,error)&&object->GetComponents<GameSproutRuntime::IBoxColliderComponent>().size()==1,error);
    Check(manager.Undo(error),"Collider remove Undo failed: "+error);
    colliders=object->GetComponents<GameSproutRuntime::IBoxColliderComponent>();
    Check(colliders.size()==2&&colliders[0]->GetComponentInstanceID()==firstLive&&colliders[1]->GetComponentInstanceID()!=secondLive,"Undo replaced the wrong Collider or reused live identity");
    Check(manager.QueryComponentByFileID(objectID,secondFile)==colliders[1]&&colliders[1]->GetShape().Size.X==2,"Undo lost Collider document identity/settings");
    Check(manager.Undo(error)&&colliders[1]->GetShape().Size.X==1,"Property Undo could not locate restored Collider by stable identity: "+error);
    Check(manager.Redo(error)&&colliders[1]->GetShape().Size.X==2,error);
    Check(manager.Redo(error)&&object->GetComponents<GameSproutRuntime::IBoxColliderComponent>().size()==1,error);
    Check(manager.Undo(error),error);
    // Prefab/source document reconciliation must update the selected repeated
    // collider in place and preserve both live identities when IDs are stable.
    colliders=object->GetComponents<GameSproutRuntime::IBoxColliderComponent>();const auto restoredSecondLive=colliders[1]->GetComponentInstanceID();
    SceneSubtreeDocument snapshot;Check(manager.CaptureObjectDocument(objectID,snapshot,error),error);
    Json::Value document;Check(AssetArtifactIO::Parse(snapshot.DocumentJson,document),"Cannot parse physical subtree");
    for(auto& component:document["Objects"][0]["Components"])if(component["ID"].asUInt64()==secondFile)component["Shape"]["Size"][0]=4;
    SceneDocumentEditRequest request;request.RootObjectID=objectID;request.DocumentJson=AssetArtifactIO::JsonText(document);
    SceneSubtreeDocument output;Check(manager.EditObjectDocument(request,output,error),error);
    colliders=object->GetComponents<GameSproutRuntime::IBoxColliderComponent>();
    Check(colliders.size()==2&&colliders[0]->GetComponentInstanceID()==firstLive&&colliders[1]->GetComponentInstanceID()==restoredSecondLive&&colliders[1]->GetShape().Size.X==4,"Document source refresh lost repeated Collider identities");
    const auto objectFile=manager.GetObjectFileID(objectID);
    Check(manager.SaveScene(error)&&manager.OpenScene(path,error),error);
    auto* reopened=manager.QueryObjectByFileID(objectFile);
    Check(reopened&&reopened->GetComponents<GameSproutRuntime::IBoxColliderComponent>().size()==2,"Saved scene lost repeated physical components");
    Check(manager.QueryComponentByFileID(reopened->GetInstanceID(),firstFile)&&manager.QueryComponentByFileID(reopened->GetInstanceID(),secondFile),"Reopen changed stable Collider identities");
    (void)runtime;
}
