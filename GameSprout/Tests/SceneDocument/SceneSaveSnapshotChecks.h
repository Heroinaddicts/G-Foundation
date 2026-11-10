#pragma once
#include <algorithm>
#include <filesystem>

static void CheckSceneSaveSnapshot(SceneManager& manager,const QDir& root,
    GameSproutRuntime::IGameSproutRuntime* runtime) {
    std::string path,error;
    Check(manager.CreateSceneAsset("Scenes","SaveSnapshot",path,error)&&manager.OpenScene(path,error),error);
    auto* scene=manager.GetActiveScene();
    auto* first=scene->CreateGameObject("Save first");
    auto* second=scene->CreateGameObject("Save second");
    Check(first&&second&&manager.SaveScene(error),error);
    const QString filename=root.filePath("Assets/"+QString::fromStdString(path));
    const auto initialBytes=Read(filename);const auto initial=Document(filename);
    const auto firstID=first->GetInstanceID(),secondID=second->GetInstanceID();
    const auto firstFile=manager.GetObjectFileID(firstID),secondFile=manager.GetObjectFileID(secondID);
    Check(firstFile&&secondFile&&!manager.HasUnsavedChanges(),"Save failed to establish stable snapshot identities");
    Check(initialBytes.contains("\n    \"Objects\"")&&initialBytes.contains("\n        {"),
        "Snapshot output lost human-readable source indentation");

    // QSaveFile replaces the inode even when its bytes happen to be identical.
    // A hard link therefore detects a redundant save deterministically without
    // waiting for filesystem timestamp granularity or asserting wall time.
    const std::filesystem::path source(filename.toStdString());
    const std::filesystem::path link(root.filePath("SavedSnapshotLink").toStdString());
    std::error_code linkError;std::filesystem::create_hard_link(source,link,linkError);
    Check(manager.SaveScene(error)&&Read(filename)==initialBytes&&!manager.HasUnsavedChanges(),error);
    if(!linkError) {
        std::error_code equivalentError;
        Check(std::filesystem::equivalent(source,link,equivalentError)&&!equivalentError,
            "Unchanged SaveScene replaced the source file instead of preserving it");
    } else std::cout<<"SKIP no-op save inode check: hard links unavailable\n";

    const auto rename=[](const char* name) {
        return [name](GameSproutRuntime::IGameObject& object,std::string&) {
            object.SetName(name);return object.GetName()==name;
        };
    };
    Check(manager.EditObject(firstID,"Snapshot rename",rename("Save renamed"),rename("Save first"),error),error);
    Check(manager.SaveScene(error)&&!manager.HasUnsavedChanges(),error);
    const auto renamed=Document(filename);
    Check(Find(renamed,"Save renamed")["ID"].asUInt64()==firstFile&&
        Find(renamed,"Save second")["ID"].asUInt64()==secondFile&&runtime->QueryGameObject(firstID)==first,
        "Incremental save changed untouched nodes or stable identities");
    if(!linkError)Check(Read(QString::fromStdString(link.string()))==initialBytes,
        "Atomic changed save modified the previous source inode");
    Check(manager.Undo(error)&&manager.HasUnsavedChanges()&&manager.SaveScene(error)&&Document(filename)==initial,
        "Undo/save did not restore original values and identities: "+error);
    Check(manager.Redo(error)&&manager.HasUnsavedChanges()&&manager.Undo(error)&&!manager.HasUnsavedChanges(),
        "Snapshot comparison lost the saved baseline after Redo/Undo: "+error);

    Check(second->SetSiblingIndex(0),"Cannot reorder snapshot fixture");
    auto* added=scene->CreateGameObject("Save added",first);Check(added,"Cannot add snapshot child");
    Check(manager.SaveScene(error),error);const auto addedFile=manager.GetObjectFileID(added->GetInstanceID());
    auto structured=Document(filename);
    Check(structured["Objects"][0]["ID"].asUInt64()==secondFile&&
        Find(structured,"Save added")["ParentID"].asUInt64()==firstFile&&addedFile,
        "Structural snapshot save omitted order, parent or new identity");
    Check(scene->DestroyGameObject(second)&&manager.SaveScene(error),error);
    structured=Document(filename);
    Check(std::none_of(structured["Objects"].begin(),structured["Objects"].end(),[secondFile](const Json::Value& node) {
        return node["ID"].asUInt64()==secondFile;
    })&&Find(structured,"Save first")["ID"].asUInt64()==firstFile&&
        Find(structured,"Save added")["ID"].asUInt64()==addedFile,
        "Snapshot save retained a deleted row or reassigned surviving identities");

    SceneDocumentEditRequest annotation;annotation.Label="Snapshot annotations";
    annotation.AnnotationsJson="{\"SaveProbe\":{\"Objects\":[],\"Name\":\"annotations only\"}}";
    SceneSubtreeDocument unused;
    Check(manager.EditObjectDocument(annotation,unused,error)&&manager.HasUnsavedChanges()&&manager.SaveScene(error),error);
    const auto annotated=Document(filename);
    const auto& probe=annotated["Annotations"]["SaveProbe"];
    Check(probe["Name"]=="annotations only"&&probe["Objects"].isArray()&&probe["Objects"].empty()&&
        annotated["Objects"]==structured["Objects"],"Annotation-only save altered the root or nested Objects array");
    Check(manager.Undo(error)&&manager.SaveScene(error)&&!Document(filename).isMember("Annotations"),
        "Annotation Undo/save retained stale snapshot header data: "+error);

    // Clean-state fast paths must still reject external byte edits. Save As is
    // permitted and must write the live snapshot even though it is not dirty.
    Check(!manager.HasUnsavedChanges(),"Snapshot fixture unexpectedly dirty");
    const auto clean=Document(filename);const auto external=Read(filename)+"\n \n";Write(filename,external);
    Check(!manager.SaveScene(error)&&!error.empty()&&Read(filename)==external&&!manager.HasUnsavedChanges(),
        "Clean save overwrote external content or changed the local baseline");
    const std::string alternate="Scenes/SaveSnapshotCopy.scene";
    Check(manager.SaveSceneAs(alternate,error)&&manager.GetActiveScenePath()==alternate&&!manager.HasUnsavedChanges(),error);
    const QString copy=root.filePath("Assets/"+QString::fromStdString(alternate));
    Check(Document(copy)==clean&&Read(filename)==external,"Clean Save As lost current data or overwrote the original");

    const auto savedCopy=Read(copy);
    first->GetTransform()->SetLocalScale({std::numeric_limits<float>::max(),1,1});
    added->GetTransform()->SetLocalPosition({std::numeric_limits<float>::max(),0,0});
    Check(!manager.SaveScene(error)&&!error.empty()&&Read(copy)==savedCopy,
        "Cached save bypassed world-transform overflow validation");
    first->GetTransform()->SetLocalScale({1,1,1});added->GetTransform()->SetLocalPosition({0,0,0});
    Check(manager.SaveScene(error)&&Document(copy)==clean,"Recovering invalid transforms corrupted the saved snapshot: "+error);

    first->SetName("Pending snapshot Play");Check(manager.HasUnsavedChanges(),"Pre-Play edit was not dirty");
    Check(manager.BeginPlaySession(error)&&manager.EndPlaySession(error),error);
    auto* restored=manager.QueryObjectByFileID(firstFile);
    Check(restored&&restored->GetName()=="Pending snapshot Play"&&manager.HasUnsavedChanges()&&manager.SaveScene(error),
        "Play restoration lost unsaved snapshot data or its stable identities: "+error);
    Check(Find(Document(copy),"Pending snapshot Play")["ID"].asUInt64()==firstFile&&
        Find(Document(copy),"Save added")["ID"].asUInt64()==addedFile,
        "Post-Play cached save lost object/component identity bindings");
}
