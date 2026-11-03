#pragma once

// Exercise real Module read/write transactions with small object graphs. The
// payloads cross the former file limits without constructing 100,000 objects.
static void CheckLargeSceneDocument(const char* runtimeDirectory) {
    Fixture fixture(runtimeDirectory);fixture.Initialize();
    constexpr std::size_t payloadBytes=101*1024*1024;
    SceneDocumentEditRequest edit;
    edit.AnnotationsJson="{\"LargeDocumentRegression\":\""+std::string(payloadBytes,'s')+"\"}";
    SceneSubtreeDocument result;std::string error;
    Check(fixture.Scenes->EditObjectDocument(edit,result,error),"Large annotations transaction failed: "+error);
    edit.AnnotationsJson.clear();edit.AnnotationsJson.shrink_to_fit();
    Check(fixture.Scenes->SaveSceneAs("Scenes/Large.scene",error),"Large Scene save failed: "+error);
    const auto filename=QString::fromStdString(fixture.Project.Root+"/Assets/Scenes/Large.scene");
    Check(QFileInfo(filename).size()>100*1024*1024,"Scene fixture did not exceed 100 MiB");
    Check(fixture.Scenes->OpenScene("Scenes/Large.scene",error),"Large Scene open failed: "+error);
    Check(fixture.Scenes->GetSceneAnnotationsJson().size()>payloadBytes,"Large Scene annotations were truncated");
    Check(fixture.Scenes->SaveScene(error),"Large Scene overwrite/hash verification failed: "+error);
    std::cout<<"Large Scene save/open/hash-check passed: "<<QFileInfo(filename).size()<<" bytes\n";
}

static void CheckLargeSourceAndTemporaryDocuments(const char* runtimeDirectory) {
    Fixture fixture(runtimeDirectory);fixture.Initialize();std::string error;
    const auto smallSource=Read(fixture.Full());
    const std::string padding(101*1024*1024,' ');
    Write(fixture.Full(),QByteArray::fromStdString(padding)+smallSource);fixture.Publish();
    Check(QFileInfo(fixture.Full()).size()>100*1024*1024,"Prefab source fixture did not exceed 100 MiB");
    const GameSproutRuntime::ResourceReference reference{SourceGuid.GetGuidString(),1};
    std::uint64_t instance=0;
    Check(fixture.Prefabs->InstantiatePrefab(reference,0,instance,error),"Large Prefab source instantiation failed: "+error);
    Check(fixture.Stage->OpenPrefab(SourceGuid.GetGuidString(),error),"Large Prefab Stage open failed: "+error);
    SetScale(*fixture.Scenes,fixture.Content()->GetInstanceID(),{2,2,2});
    Check(fixture.Stage->Save(error),"Large Prefab Stage atomic save/hash verification failed: "+error);
    Check(fixture.Stage->Close(false,error),error);
    const auto changedSource=Read(fixture.Full());
    Write(fixture.Full(),QByteArray::fromStdString(padding)+changedSource);fixture.Publish();
    auto* object=fixture.Runtime->QueryGameObject(instance);Check(object,"Large source instance was lost");
    SetScale(*fixture.Scenes,object->GetChild(0)->GetInstanceID(),{3,3,3});
    Check(fixture.Prefabs->ApplyOverrides(instance,error),"Large Prefab Apply/hash verification failed: "+error);
    Json::Value source=Parse(Read(fixture.Full()));
    const auto& data=source["Data"];
    {
        SceneDocumentSessionRequest temporary;
        temporary.DocumentJson=padding+Text(data);temporary.ContentRootFileID=PrefabDefinition::RootID(data);
        Check(fixture.Scenes->BeginDocumentSession(temporary,error),"Large temporary document failed: "+error);
        Check(fixture.Scenes->EndDocumentSession(error),error);
    }
    {
        SceneDocumentEditRequest insertion;insertion.DocumentJson=padding+Text(data);insertion.RemapDocumentIDs=true;
        SceneSubtreeDocument inserted;
        Check(fixture.Scenes->EditObjectDocument(insertion,inserted,error),"Large subtree insertion failed: "+error);
        Check(inserted.RootObjectID&&fixture.Scenes->Undo(error),"Large subtree transaction could not be undone: "+error);
    }
    std::cout<<"Large Prefab source/Stage/Apply and temporary/subtree documents passed (>100 MiB input)\n";
}

static void CheckLargePreparedPrefab(const char* runtimeDirectory) {
    Fixture fixture(runtimeDirectory);fixture.Initialize();std::string error;
    {
        auto source=Parse(Read(fixture.Full()));auto& data=source["Data"];
        const auto identity=data["NextObjectID"].asUInt64();
        Json::Value component;component["ID"]=Json::UInt64(identity);component["Type"]="GameSproutRuntime.ScriptComponent";
        component["InstanceID"]=Json::UInt64(1);component["ScriptTypeID"]="LargeDocumentMissingScript";
        component["Source"]["Guid"]="3456789012abcdef3456789012abcdef";component["Source"]["LocalID"]=Json::UInt64(1);component["Enabled"]=false;
        // Each field satisfies the current ScriptValue schema; their combined
        // prepared document crosses the former independent 64 MiB artifact cap.
        const std::string chunk(1024*1024,'p');
        for(unsigned index=0;index<65;++index) {
            auto& field=component["Fields"]["Chunk"+std::to_string(index)];field["Kind"]="String";field["Value"]=chunk;
        }
        data["Objects"][0]["Components"].append(std::move(component));data["NextObjectID"]=Json::UInt64(identity+1);
        Write(fixture.Full(),QByteArray::fromStdString(Text(source)));
    }
    fixture.Publish();
    Check(QFileInfo(QString::fromStdString(fixture.Library.Asset->Path)).size()>64*1024*1024,"Prepared Prefab fixture did not exceed 64 MiB");
    const GameSproutRuntime::ResourceReference reference{SourceGuid.GetGuidString(),1};std::uint64_t instance=0;
    Check(fixture.Prefabs->InstantiatePrefab(reference,0,instance,error),"Large prepared Prefab instantiation failed: "+error);
    auto* root=fixture.Runtime->QueryGameObject(instance);Check(root,"Large prepared Prefab root missing");
    auto* script=ScriptAt(root);Check(script&&script->GetFields().size()==65,"Large prepared Prefab fields were truncated");
    Check(fixture.Stage->OpenPrefab(SourceGuid.GetGuidString(),error),"Large data-bearing Prefab Stage failed: "+error);
    script=ScriptAt(fixture.Content());Check(script&&script->GetFields().size()==65,"Large Stage fields were truncated");
    Check(fixture.Stage->Close(false,error),error);
    std::cout<<"Large prepared Prefab instantiation/Stage passed: "<<fixture.Library.Asset->Size<<" bytes\n";
}
