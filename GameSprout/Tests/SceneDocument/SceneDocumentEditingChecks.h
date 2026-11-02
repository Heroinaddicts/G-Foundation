#pragma once

static void CheckDocumentEditing(SceneManager& manager,const QDir& root,GameSproutRuntime::IGameSproutRuntime* runtime) {
    std::string path,error;
    Check(manager.CreateSceneAsset("Scenes","Document transactions",path,error)&&manager.OpenScene(path,error),error);
    auto* scene=manager.GetActiveScene();
    auto* source=scene->CreateGameObject("Source subtree");auto* child=scene->CreateGameObject("Source child");
    Check(source->AddChild(child,false),"Cannot create document subtree");
    auto* outside=scene->CreateGameObject("Outside reference owner");
    const auto sourceID=source->GetInstanceID(),outsideID=outside->GetInstanceID();
    const GameSproutRuntime::ResourceReference scriptSource{"3456789012abcdef3456789012abcdef",1};
    Check(manager.AttachScriptComponent(outsideID,scriptSource.Guid,scriptSource,error),error);
    SceneSubtreeDocument captured;
    Check(manager.CaptureObjectDocument(sourceID,captured,error),error);
    Json::Value original;Check(AssetArtifactIO::Parse(captured.DocumentJson,original),"Cannot parse captured document");
    Check(original["Objects"].size()==2&&captured.ObjectIDs.size()==4&&original["NextObjectID"].asUInt64()>original["Objects"][1]["ID"].asUInt64(),
          "Capture omitted stable object/component identities or allocation high water");
    const auto sourceFile=manager.GetObjectFileID(sourceID);
    SceneDocumentEditRequest insert;insert.DocumentJson=captured.DocumentJson;insert.RemapDocumentIDs=true;insert.Label="Insert document";
    insert.BuildAnnotations=[](const SceneSubtreeDocument& result,const std::string&,std::string& text,std::string&) {
        Json::Value data(Json::objectValue);data["Owner"]="Document transaction check";data["Mappings"]=Json::Value(Json::arrayValue);
        for(const auto& identity:result.ObjectIDs) {Json::Value pair(Json::objectValue);pair["Source"]=Json::UInt64(identity.DocumentFileID);pair["Scene"]=Json::UInt64(identity.SceneFileID);pair["Component"]=identity.Component;data["Mappings"].append(pair);}
        text=AssetArtifactIO::JsonText(data);return true;
    };
    SceneSubtreeDocument inserted;
    Check(manager.EditObjectDocument(insert,inserted,error),error);
    const auto insertedFile=manager.GetObjectFileID(inserted.RootObjectID);
    Check(inserted.RootObjectID&&insertedFile!=sourceFile&&inserted.ObjectIDs.size()==4&&manager.QueryObjectByFileID(sourceFile)==source,
          "Insertion reused identities or lost the original subtree");
    const std::string annotations=manager.GetSceneAnnotationsJson();
    Check(manager.Undo(error)&&!manager.QueryObjectByFileID(insertedFile)&&manager.GetSceneAnnotationsJson()!=annotations&&runtime->QueryGameObject(outsideID)==outside,
          "Document Undo did not atomically remove subtree/annotations");
    Check(manager.Redo(error)&&manager.QueryObjectByFileID(insertedFile)&&manager.GetSceneAnnotationsJson()==annotations,error);
    auto* instance=manager.QueryObjectByFileID(insertedFile);const auto instanceID=instance->GetInstanceID();
    auto* instanceChild=instance->GetChild(0);const auto childID=instanceChild->GetInstanceID(),childFile=manager.GetObjectFileID(childID);
    auto* transform=instance->GetTransform();auto* script=ScriptAt(*outside,0);
    GameSproutRuntime::ScriptValue reference;reference.Kind=GameSproutRuntime::ScriptValueKind::SceneObject;reference.SceneObject={true,childFile};
    Check(script&&script->SetField("Test::_Target",reference,error),error);
    SceneSubtreeDocument current;Check(manager.CaptureObjectDocument(instanceID,current,error),error);
    Json::Value replacement;Check(AssetArtifactIO::Parse(current.DocumentJson,replacement),"Cannot parse replacement");
    replacement["Objects"][0]["Name"]="Updated instance";
    replacement["Objects"][0]["Layer"]=Json::UInt(2);
    replacement["Objects"][0]["Components"][0]["Position"][0]=5.0f;
    const auto next=replacement["NextObjectID"].asUInt64();
    Json::Value added=replacement["Objects"][1];added["ID"]=Json::UInt64(next);added["Name"]="New sibling";
    added["Components"][0]["ID"]=Json::UInt64(next+1);replacement["Objects"].append(added);
    replacement["NextObjectID"]=Json::UInt64(next+2);
    SceneDocumentEditRequest replace;replace.RootObjectID=instanceID;replace.DocumentJson=AssetArtifactIO::JsonText(replacement);replace.AnnotationsJson="{\"Generation\":2}";
    SceneSubtreeDocument replaced;Check(manager.EditObjectDocument(replace,replaced,error),error);
    Check(replaced.RootObjectID==instanceID&&manager.QueryObjectByFileID(childFile)->GetInstanceID()==childID&&instance->GetTransform()==transform&&
          instance->GetName()=="Updated instance"&&instance->GetLayer()==2&&instance->GetChild(0)==instanceChild&&instance->GetChild(1)->GetName()=="New sibling"&&
          script->GetFields().at("Test::_Target")==reference&&runtime->QuerySceneObjectByFileID(scene,childFile)==instanceChild,
          "Replacement rebuilt unchanged identities, changed order, or broke external script references");
    outside->SetName("Unrelated later edit");
    Check(manager.Undo(error),"Replacement Undo failed: "+error);
    Check(instance->GetName()=="Source subtree"&&instance->GetLayer()==0&&instance->GetChildCount()==1&&outside->GetName()=="Unrelated later edit"&&
          manager.GetSceneAnnotationsJson()==annotations,"Replacement Undo lost unrelated edits or annotations");
    Check(manager.Redo(error)&&instance->GetName()=="Updated instance"&&instance->GetLayer()==2&&instance->GetChildCount()==2,error);
    SceneSubtreeDocument beforeFailure;Check(manager.CaptureObjectDocument(instanceID,beforeFailure,error),error);
    const auto beforeAnnotations=manager.GetSceneAnnotationsJson();const auto children=scene->GetChildCount();
    SceneDocumentEditRequest invalid=insert;invalid.BuildAnnotations=[](const SceneSubtreeDocument&,const std::string&,std::string&,std::string& reason){reason="Rejected annotations";return false;};
    SceneSubtreeDocument unused;Check(!manager.EditObjectDocument(invalid,unused,error)&&scene->GetChildCount()==children&&manager.GetSceneAnnotationsJson()==beforeAnnotations,
          "Rejected annotations changed the scene");
    Json::Value overflow;Check(AssetArtifactIO::Parse(beforeFailure.DocumentJson,overflow),"Cannot parse rollback fixture");
    overflow["Objects"][0]["Components"][0]["Scale"][0]=std::numeric_limits<float>::max();
    overflow["Objects"][1]["Components"][0]["Position"][0]=std::numeric_limits<float>::max();
    const auto failureNext=overflow["NextObjectID"].asUInt64();
    auto transient=overflow["Objects"][1];transient["ID"]=Json::UInt64(failureNext);transient["Components"][0]["ID"]=Json::UInt64(failureNext+1);
    overflow["Objects"].append(transient);overflow["NextObjectID"]=Json::UInt64(failureNext+2);
    invalid={};invalid.RootObjectID=instanceID;invalid.DocumentJson=AssetArtifactIO::JsonText(overflow);invalid.AnnotationsJson="{\"Invalid\":true}";
    Check(!manager.EditObjectDocument(invalid,unused,error)&&runtime->QueryGameObject(instanceID)==instance&&instance->GetTransform()==transform&&manager.GetSceneAnnotationsJson()==beforeAnnotations,
          "Failed document patch did not roll back properties/annotations");
    SceneSubtreeDocument afterFailure;Check(manager.CaptureObjectDocument(instanceID,afterFailure,error)&&afterFailure.DocumentJson==beforeFailure.DocumentJson,
          "Failed document patch changed the authored subtree");
    Check(manager.SetObjectLayer(instanceID,3,true,error)&&instance->GetLayer()==3&&instanceChild->GetLayer()==3,error);
    Check(manager.Undo(error)&&instance->GetLayer()==2&&instanceChild->GetLayer()==0,"Subtree Layer change was not one Undo transaction");
    Check(manager.Redo(error)&&instance->GetLayer()==3&&instanceChild->GetLayer()==3,error);
    Check(manager.Undo(error),error);
    auto settings=runtime->GetProjectSettings();const auto oldTag=settings.NextTagID++;
    settings.Tags.push_back({oldTag,"HistoryOnlyTag"});Check(runtime->SetProjectSettings(settings,error)&&instance->SetTagID(oldTag),error);
    Check(manager.EditObject(instanceID,"Clear Tag",[](GameSproutRuntime::IGameObject& node,std::string&){return node.SetTagID(0);},
        [oldTag](GameSproutRuntime::IGameObject& node,std::string&){return node.SetTagID(oldTag);},error),error);
    const auto references=manager.GetReferencedTagIDs();
    Check(std::find(references.begin(),references.end(),oldTag)!=references.end(),"Project definition checks lost Undo-only Tag reference");
    Check(manager.Undo(error)&&instance->GetTagID()==oldTag&&manager.Redo(error)&&instance->GetTagID()==0,error);
    Check(manager.SaveScene(error),error);
    const auto saved=Document(root.filePath("Assets/"+QString::fromStdString(path)));
    Check(saved["Annotations"]["Generation"].asInt()==2,"Save omitted annotations");
    Check(manager.BeginPlaySession(error),error);
    Check(!manager.EditObjectDocument(insert,unused,error)&&manager.GetSceneAnnotationsJson()==beforeAnnotations,"Play accepted a document edit or lost annotations");
    Check(manager.EndPlaySession(error)&&manager.GetSceneAnnotationsJson()==beforeAnnotations,error);
    Check(manager.OpenScene(path,error)&&manager.GetSceneAnnotationsJson()==beforeAnnotations&&!manager.HasUnsavedChanges(),
          "Open/Play round trip lost annotations or dirtied baseline");
}
