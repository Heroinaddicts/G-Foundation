#pragma once

static void CheckScriptReferences(SceneManager& manager,const QDir& root,GameSproutRuntime::IGameSproutRuntime* runtime) {
    std::string path,error;
    Check(manager.CreateSceneAsset("Scenes","Script references",path,error)&&manager.OpenScene(path,error),error);
    auto* scene=manager.GetActiveScene();
    auto* owner=scene->CreateGameObject("Script owner");
    auto* target=scene->CreateGameObject("Referenced node");
    const auto ownerID=owner->GetInstanceID(),targetID=target->GetInstanceID();
    const GameSproutRuntime::ResourceReference source{"3456789012abcdef3456789012abcdef",1};
    Check(manager.AttachScriptComponent(ownerID,source.Guid,source,error),error);
    auto* script=ScriptAt(*owner,0);Check(script,"Reference fixture has no script");
    Check(manager.SaveScene(error),error);
    const auto ownerFileID=manager.GetObjectFileID(ownerID),targetFileID=manager.GetObjectFileID(targetID);
    uint64_t runtimeFileID=0;
    Check(runtime->GetSceneObjectFileID(target,runtimeFileID)&&runtimeFileID==targetFileID&&
          runtime->QuerySceneObjectByFileID(scene,targetFileID)==target,"Save did not publish stable object identities to Runtime");
    GameSproutRuntime::ScriptValue reference;reference.Kind=GameSproutRuntime::ScriptValueKind::SceneObject;
    reference.SceneObject={true,targetFileID};
    GameSproutRuntime::ScriptValue rootReference=reference;rootReference.SceneObject.ObjectFileID=0;
    GameSproutRuntime::ScriptValue emptyReference=reference;emptyReference.SceneObject={};
    Check(script->SetField("Test::_Test",reference,error)&&script->SetField("Test::_Scene",rootReference,error)&&
          script->SetField("Test::_Optional",emptyReference,error)&&manager.SaveScene(error),error);
    const auto filename=root.filePath("Assets/"+QString::fromStdString(path));
    const auto document=Document(filename);
    const auto& node=Find(document,"Script owner");
    Json::Value fields;
    for(const auto& component:node["Components"])if(component["Type"]=="GameSproutRuntime.ScriptComponent")fields=component["Fields"];
    Check(fields["Test::_Test"]["Kind"]=="SceneObject"&&fields["Test::_Test"]["Value"]["ObjectFileID"].asUInt64()==targetFileID&&
          fields["Test::_Scene"]["Value"]["ObjectFileID"].asUInt64()==0&&fields["Test::_Optional"]["Value"].isNull(),
          "Scene references confused empty with scene-root ID zero or wrote process identities");
    Check(manager.OpenScene(path,error),error);
    scene=manager.GetActiveScene();owner=manager.QueryObjectByFileID(ownerFileID);script=owner?ScriptAt(*owner,0):nullptr;
    target=manager.QueryObjectByFileID(targetFileID);
    Check(owner&&script&&target&&owner->GetInstanceID()!=ownerID&&target->GetInstanceID()!=targetID&&
          script->GetFields().at("Test::_Test")==reference&&runtime->QuerySceneObjectByFileID(scene,targetFileID)==target,
          "Scene reopen did not remap a stable reference to new Runtime identities");
    const auto reopenedOwnerID=owner->GetInstanceID();
    Check(manager.DeleteObject(target->GetInstanceID(),error)&&!runtime->QuerySceneObjectByFileID(scene,targetFileID),error);
    Check(script->GetFields().at("Test::_Test")==reference&&manager.SaveScene(error),
          "Deleting a target discarded authored identity instead of retaining Missing");
    Check(manager.Undo(error),error);
    target=manager.QueryObjectByFileID(targetFileID);
    Check(target&&runtime->QuerySceneObjectByFileID(scene,targetFileID)==target&&owner->GetInstanceID()==reopenedOwnerID&&
          script->GetFields().at("Test::_Test")==reference,"Undo deletion did not preserve/rebind object identity");
    Check(manager.BeginPlaySession(error),error);
    scene=manager.GetActiveScene();owner=manager.QueryObjectByFileID(ownerFileID);target=manager.QueryObjectByFileID(targetFileID);
    script=owner?ScriptAt(*owner,0):nullptr;
    Check(script&&target&&owner->GetInstanceID()!=reopenedOwnerID&&runtime->QuerySceneObjectByFileID(scene,targetFileID)==target&&
          script->GetFields().at("Test::_Test")==reference,"Play cloning reused editing pointers or lost FileID references");
    Check(manager.EndPlaySession(error),error);
    auto invalid=document;
    for(auto& item:invalid["Objects"])if(item["Name"]=="Script owner")
        for(auto& component:item["Components"])if(component["Type"]=="GameSproutRuntime.ScriptComponent")
            component["Fields"]["Test::_Test"]["Value"]["ObjectFileID"]=-1;
    Check(!SceneDocument::Validate(invalid,error),"Negative scene reference identity was accepted");
}
