#pragma once
#include <QElapsedTimer>

static void CheckIncrementalSceneState(SceneManager& manager,GameSproutRuntime::IGameSproutRuntime* runtime) {
    std::string path,error;
    Check(manager.CreateSceneAsset("Scenes","LargeIncremental",path,error)&&manager.OpenScene(path,error),error);
    auto* scene=manager.GetActiveScene();
    for(unsigned index=0;index<5000;++index)Check(scene->CreateGameObject("Node "+std::to_string(index)),"Cannot create large authored fixture");
    Check(manager.SaveScene(error),error);
    auto* camera=scene->GetMainCamera();auto* object=camera->GetGameObject();
    const auto objectID=object->GetInstanceID(),fileID=manager.GetObjectFileID(objectID);
    QElapsedTimer timer;timer.start();
    for(unsigned query=0;query<1000;++query) {
        Check(!manager.HasUnsavedChanges(),"Repeated dirty queries changed a saved scene");
        Check(timer.elapsed()<1000,"Unchanged dirty queries serialized or traversed the large scene");
    }
    const auto before=camera->GetSettings();auto after=before;after.VerticalFovRadians+=.01f;
    const auto write=[](GameSproutRuntime::CameraSettings settings) {
        return [settings](GameSproutRuntime::IGameObject& target,std::string&) {
            auto* value=target.GetComponent<GameSproutRuntime::ICameraComponent>();return value&&value->SetSettings(settings);
        };
    };
    timer.restart();
    Check(manager.EditObject(objectID,"Large scene FOV",write(after),write(before),error),error);
    Check(timer.elapsed()<1000,"One Camera property edit serialized the large scene");
    Check(manager.HasUnsavedChanges(),"Camera edit was not dirty");timer.restart();
    Check(manager.Undo(error)&&!manager.HasUnsavedChanges(),"Incremental Undo did not restore the saved values: "+error);
    Check(timer.elapsed()<1000,"One property Undo serialized the large scene");
    Check(runtime->QueryGameObject(objectID)==object&&manager.GetObjectFileID(objectID)==fileID,
          "Incremental properties replaced nodes or changed stable file identities");
    // No Update/dispatch: setters must be visible at the exact next query.
    const auto savedName=object->GetName();object->SetName("Direct unsent rename");
    Check(manager.HasUnsavedChanges(),"Undispatched direct rename was not observed");
    object->SetName(savedName);Check(!manager.HasUnsavedChanges(),"Direct restoration did not compare against the saved node values");
    auto* external=scene->CreateGameObject("External structure");
    Check(external&&manager.HasUnsavedChanges(),"External structure did not invalidate the authored snapshot");
    Check(manager.SaveScene(error),error);
    Check(scene->DestroyGameObject(external)&&manager.HasUnsavedChanges(),"External deletion was not observed");
    // Journal readers have a bounded history. A missed window must rebuild once,
    // not silently report clean or invent an Undo transaction for external edits.
    Check(manager.SaveScene(error),error);
    for(unsigned index=0;index<2050;++index)object->SetName("Missed event "+std::to_string(index));
    Check(manager.HasUnsavedChanges(),"Overflowed authored journal lost external changes");
    object->SetName(savedName);Check(!manager.HasUnsavedChanges(),"Snapshot fallback could not return to baseline");
}
