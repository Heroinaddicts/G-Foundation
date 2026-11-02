#pragma once

// These descriptors deliberately cannot construct gameplay code. Stage loading
// needs current schema metadata, not execution of a user's native script DLL.
struct MetadataOnlyIdentity {};
static int MetadataNativeCalls=0;
static GameSproutRuntime::ScriptFieldRegistration MetadataField(const std::string& id,const GameSproutRuntime::ScriptValue& value) {
    GameSproutRuntime::ScriptFieldRegistration field;field.Info.ID=id;field.Info.Name=id;field.Info.Default=value;
    field.Read=[](const GameSproutRuntime::IScript&,GameSproutRuntime::ScriptValue&,const GameSproutRuntime::ScriptReferenceContext&,std::string&){return false;};
    field.Write=[](GameSproutRuntime::IScript&,const GameSproutRuntime::ScriptValue&,const GameSproutRuntime::ScriptReferenceContext&,std::string&){return false;};
    field.Validate=[](const GameSproutRuntime::ScriptValue&,const GameSproutRuntime::ScriptReferenceContext&,std::string& error){error.clear();return true;};
    return field;
}
static GameSproutRuntime::ScriptTypeRegistration CurrentMetadata(const std::string& typeID,const GameSproutRuntime::ResourceReference& source,bool evolved) {
    GameSproutRuntime::ScriptTypeRegistration type;type.Info.TypeID=typeID;type.Info.Name="MetadataOnly";type.Info.SourceFile="MetadataOnly.hpp";
    type.CppType=&typeid(MetadataOnlyIdentity);type.Source=source;
    type.Create=[](GameSproutRuntime::IGameObject*)->GameSproutRuntime::IScript*{++MetadataNativeCalls;return nullptr;};
    type.Destroy=[](GameSproutRuntime::IScript*){++MetadataNativeCalls;};
    GameSproutRuntime::ScriptValue target;target.Kind=GameSproutRuntime::ScriptValueKind::SceneObject;
    type.Fields.push_back(MetadataField("Test::_Target",target));
    if(evolved) {
        GameSproutRuntime::ScriptValue number;number.Kind=GameSproutRuntime::ScriptValueKind::Number;number.Number=6.25;
        GameSproutRuntime::ScriptValue added;added.Kind=GameSproutRuntime::ScriptValueKind::Integer;added.Integer=42;
        type.Fields.push_back(MetadataField("Test::_Changed",number));type.Fields.push_back(MetadataField("Test::_NewField",added));
    }
    return type;
}
static GameSproutRuntime::IScriptComponent* ScriptAt(GameSproutRuntime::IGameObject* object) {
    if(object)for(std::size_t index=0;index<object->GetComponentCount();++index)
        if(auto* script=dynamic_cast<GameSproutRuntime::IScriptComponent*>(object->GetComponentAt(index)))return script;
    return nullptr;
}
static const Json::Value& ScriptData(const Json::Value& source) {
    for(const auto& node:source["Data"]["Objects"])for(const auto& component:node["Components"])
        if(component["Type"]=="GameSproutRuntime.ScriptComponent")return component;
    throw std::runtime_error("Source has no script component");
}
static void CheckProjectedFields(GameSproutRuntime::IScriptComponent* script,bool evolved) {
    Check(script&&script->IsScriptTypeAvailable(),"Current script metadata was not recognized in the Prefab Stage");
    const auto& fields=script->GetFields();
    Check(fields.size()==(evolved?3:1)&&fields.count("Test::_Target")&&!fields.count("Test::_RemovedBool"),"Stage retained removed fields or lost a current field");
    if(evolved) {
        Check(fields.at("Test::_Changed").Kind==GameSproutRuntime::ScriptValueKind::Number&&fields.at("Test::_Changed").Number==6.25,
            "Changed field kind did not use the current schema default");
        Check(fields.at("Test::_NewField").Kind==GameSproutRuntime::ScriptValueKind::Integer&&fields.at("Test::_NewField").Integer==42,
            "A new current field did not receive its schema default");
    }
}
static void CheckScriptFieldEvolution(Fixture& fixture) {
    const std::string typeID="stage-schema-evolution";
    const GameSproutRuntime::ResourceReference scriptSource{"3456789012abcdef3456789012abcdef",1};
    auto source=Parse(Read(fixture.Full()));auto& data=source["Data"];auto& root=data["Objects"][0];
    const auto childID=data["Objects"][1]["ID"].asUInt64(),scriptID=data["NextObjectID"].asUInt64();
    Json::Value script;script["ID"]=Json::UInt64(scriptID);script["Type"]="GameSproutRuntime.ScriptComponent";script["InstanceID"]=Json::UInt64(1);
    script["ScriptTypeID"]=typeID;script["Source"]["Guid"]=scriptSource.Guid;script["Source"]["LocalID"]=Json::UInt64(1);script["Enabled"]=true;
    script["Fields"]["Test::_Target"]["Kind"]="SceneObject";script["Fields"]["Test::_Target"]["Value"]["ObjectFileID"]=Json::UInt64(childID);
    script["Fields"]["Test::_RemovedBool"]["Kind"]="Boolean";script["Fields"]["Test::_RemovedBool"]["Value"]=true;
    script["Fields"]["Test::_Changed"]["Kind"]="Boolean";script["Fields"]["Test::_Changed"]["Value"]=true;
    root["Components"].append(script);data["NextObjectID"]=Json::UInt64(scriptID+1);
    const auto oldBytes=QByteArray::fromStdString(Text(source));Write(fixture.Full(),oldBytes);fixture.Publish();
    std::string error;Check(fixture.Prefabs->RefreshSource(SourceGuid.GetGuidString(),error),"Cannot prepare old-schema scene instances: "+error);
    auto* first=fixture.Runtime->QueryGameObject(fixture.FirstID);auto* second=fixture.Runtime->QueryGameObject(fixture.SecondID);
    Check(ScriptAt(first)&&ScriptAt(first)->GetFields().count("Test::_RemovedBool"),"Missing script type did not retain serialized values before its schema was registered");
    Check(fixture.Runtime->RegisterScriptType(CurrentMetadata(typeID,scriptSource,true),error),error);
    CheckProjectedFields(ScriptAt(first),true);CheckProjectedFields(ScriptAt(second),true);
    const auto mainField=ScriptAt(first)->GetFields().at("Test::_Target");const auto metaBytes=Read(fixture.Full()+".meta");
    Check(fixture.Stage->OpenPrefab(SourceGuid.GetGuidString(),error),"Old-schema Prefab Stage open failed: "+error);
    auto* content=fixture.Content();CheckProjectedFields(ScriptAt(content),true);
    const auto target=ScriptAt(content)->GetFields().at("Test::_Target").SceneObject;
    Check(target.Assigned&&fixture.Runtime->QuerySceneObjectByFileID(fixture.Scenes->GetActiveScene(),target.ObjectFileID)==content->GetChild(0),
        "Schema projection lost the compatible internal object reference");
    Check(Read(fixture.Full())==oldBytes&&MetadataNativeCalls==0,"Opening Stage rewrote the source or executed native script code");
    SetScale(*fixture.Scenes,content->GetChild(0)->GetInstanceID(),{1,8,1});
    Check(fixture.Stage->Close(true,error)&&Read(fixture.Full())==oldBytes,"Discard persisted schema projection or edits to source");
    Check(fixture.Stage->OpenPrefab(SourceGuid.GetGuidString(),error),error);content=fixture.Content();
    SetScale(*fixture.Scenes,content->GetChild(0)->GetInstanceID(),{1,8,1});
    Check(fixture.Stage->Save(error),"Saving a schema-evolved Stage failed: "+error);
    const auto saved=Parse(Read(fixture.Full()));const auto& savedScript=ScriptData(saved);const auto& fields=savedScript["Fields"];
    Check(fields.size()==3&&!fields.isMember("Test::_RemovedBool")&&fields["Test::_Changed"]["Kind"]=="Number"&&
          fields["Test::_NewField"]["Value"].asInt64()==42,"Stage save failed to persist exactly the current field schema/defaults");
    Check(savedScript["ID"].asUInt64()==scriptID&&saved["Data"]["Objects"][0]["ID"]==root["ID"]&&Read(fixture.Full()+".meta")==metaBytes,
        "Schema-evolved save changed source GUID, root or component identities");
    CheckProjectedFields(ScriptAt(first),true);CheckProjectedFields(ScriptAt(second),true);
    Check(ScriptAt(first)->GetFields().at("Test::_Target")==mainField&&first->GetChild(0)->GetTransform()->GetLocalPosition().X==7&&
          second->GetChild(0)->GetTransform()->GetLocalPosition().X==9&&first->GetChild(0)->GetTransform()->GetLocalScale().Y==8,
        "Old instance baselines prevented source propagation or lost compatible reference/property overrides");
    Check(fixture.Stage->GetState().Error.empty()&&fixture.Stage->Close(false,error)&&MetadataNativeCalls==0,
        "Schema-evolved source propagation reported errors or executed gameplay code: "+error);
    Check(fixture.Runtime->UnregisterScriptType(typeID,error),error);
}
static void CheckUserPrefabSchema(Fixture& fixture,const QString& userSourcePath) {
    // Only read the supplied source and repackage it under this fixture's GUID;
    // never load GameLogic, render resource contents, or modify the user project.
    const auto bytes=Read(userSourcePath);const auto source=Parse(bytes);const auto& definition=ScriptData(source);
    const std::string typeID=definition["ScriptTypeID"].asString();
    const GameSproutRuntime::ResourceReference scriptSource{definition["Source"]["Guid"].asString(),definition["Source"]["LocalID"].asUInt64()};
    std::string error;Check(fixture.Runtime->RegisterScriptType(CurrentMetadata(typeID,scriptSource,false),error),error);
    Write(fixture.Full(),bytes);fixture.Publish();
    Check(fixture.Stage->OpenPrefab(SourceGuid.GetGuidString(),error),"Copied user source cannot enter Stage with the current Target-only schema: "+error);
    auto* content=fixture.Content();auto* script=ScriptAt(content);CheckProjectedFields(script,false);
    const auto& original=definition["Fields"]["Test::_Target"]["Value"];const auto restored=script->GetFields().at("Test::_Target").SceneObject;
    if(original.isNull())Check(!restored.Assigned,"Copied null Target became an assigned scene object");
    else Check(restored.Assigned&&fixture.Runtime->QuerySceneObjectByFileID(fixture.Scenes->GetActiveScene(),restored.ObjectFileID)==content->GetChild(0),
        "Copied user Prefab lost its internal Target during schema projection");
    Check(fixture.Stage->Close(true,error)&&Read(fixture.Full())==bytes&&Read(userSourcePath)==bytes&&MetadataNativeCalls==0,
        "Reading/discarding the copied user source modified source data or executed gameplay code");
    Check(fixture.Runtime->UnregisterScriptType(typeID,error),error);
    std::cout<<"PASS copied user Prefab Stage with current Target-only metadata: "<<QFileInfo(userSourcePath).fileName().toStdString()<<'\n';
}
