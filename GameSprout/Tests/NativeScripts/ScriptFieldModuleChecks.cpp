#include "ScriptFieldModuleTestServices.h"

#include "ScriptRecursiveFieldsChecks.h"

int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        Check(argc==2,"Pass Runtime directory");runtime=Export(argv[1]);Check(runtime,"Runtime unavailable");
        GameSproutRuntime::Internal::RegisterScriptClass<FieldsBehaviour>("1234567890abcdef1234567890abcdef","FieldsBehaviour","Fixture.hpp",{},
            {GameSproutRuntime::Internal::MakeField<FieldsBehaviour>("FieldsBehaviour::Speed","Speed",&FieldsBehaviour::Speed,1.f),
             GameSproutRuntime::Internal::MakeField<FieldsBehaviour>("FieldsBehaviour::Target","Target",&FieldsBehaviour::Target,static_cast<GameSproutRuntime::IGameObject*>(nullptr))});
        const auto registration=GameSproutRuntime::Internal::ScriptModuleTypes().back();
        std::string error;Check(runtime->RegisterScriptType(registration,error),error);
        SceneService scenes;SelectionService selection;ScriptEditor editor;ModeService mode;HostService host;ObjectEditing editing;
        editing._Scenes=&scenes;editing._Play=&mode;editing._Host=&host;editing._Runtime=runtime;editing._OwnerThread=QThread::currentThread();editing._Ready=true;
        scenes.Runtime=runtime;scenes.Scene=runtime->CreateScene("Fields");
        auto* owner=scenes.Scene->CreateGameObject("Owner");auto* target=scenes.Scene->CreateGameObject("Target");
        Check(runtime->BindSceneObjectIdentity(scenes.Scene,0,error)&&runtime->BindSceneObjectIdentity(owner,10,error)&&
              runtime->BindSceneObjectIdentity(target,20,error),error);
        auto* script=runtime->AddScriptComponent(owner,registration.Info.TypeID,{registration.Info.TypeID,1},1,error);Check(script,error);
        editor._Runtime=runtime;editor._Scenes=&scenes;editor._Selection=&selection;editor._Editing=&editing;editor._Launched=true;
        scenes.Changed=[&editor]{editor.OnSceneStateChanged();};
        editor._Types.push_back({registration.Info,{registration.Info.TypeID,1},"Fixture.hpp"});
        Listener listener;Check(editor.RegisterScriptListener(&listener),"Listener registration");
        ScriptPropertySnapshot snapshot;
        Check(editor.ReadFields(owner->GetInstanceID(),1,snapshot,error)&&snapshot.Fields.size()==2,error);
        const auto scope=snapshot.Target;
        Check(editor.ObserveFields(scope,&listener),"Observe scoped script fields");
        std::vector<ScriptObjectChoice> choices;Check(editor.GetSceneObjectChoices(scope,choices,error)&&choices.size()==3,error);
        ScriptObjectTarget sourceTarget;Check(editor.ReadObjectTarget(target->GetInstanceID(),sourceTarget,error),error);
        GameSproutRuntime::ScriptValue value;Check(editor.ReferenceFromObject(scope,sourceTarget,value,error),error);
        Check(value.SceneObject.Assigned&&value.SceneObject.ObjectFileID==20,"Drag conversion leaked Runtime identity");
        Check(editor.SetField(scope,"FieldsBehaviour::Target",value,error)&&listener.Changes>0,error);
        Check(script->GetFields().at("FieldsBehaviour::Target")==value&&scenes.CanUndo(),"Field edit bypassed scene transaction");
        Check(editor.ReadFields(scope.ObjectID,1,snapshot,error),error);
        Check(snapshot.Fields[1].ObjectName=="Target"&&!snapshot.Fields[1].MissingObject,"Reference snapshot lacks target label");
        Check(editor.LocateObjectReference(scope,value,error)&&selection.SceneSelection.ObjectID==target->GetInstanceID(),error);
        Check(scenes.Undo(error)&&!script->GetFields().at("FieldsBehaviour::Target").SceneObject.Assigned&&scenes.Redo(error),error);
        auto* foreign=runtime->CreateScene("Foreign");auto* foreignObject=foreign->CreateGameObject("Other");
        Check(!editor.ReadObjectTarget(foreignObject->GetInstanceID(),sourceTarget,error),"Foreign-scene source received a local scope");
        const ScriptObjectTarget foreignTarget{scope.SessionID,foreign->GetInstanceID(),foreignObject->GetInstanceID()};
        Check(!editor.ReferenceFromObject(scope,foreignTarget,value,error),"Foreign-scene drag accepted");
        Check(scenes.Scene->DestroyGameObject(target),"Delete target");
        Check(editor.ReadFields(scope.ObjectID,1,snapshot,error)&&snapshot.Fields[1].MissingObject&&
              snapshot.Fields[1].Value.SceneObject.ObjectFileID==20,"Deletion lost Missing reference identity");
        auto* replacement=scenes.Scene->CreateGameObject("Restored target");
        Check(runtime->BindSceneObjectIdentity(replacement,20,error)&&editor.ReadFields(scope.ObjectID,1,snapshot,error)&&
              !snapshot.Fields[1].MissingObject&&snapshot.Fields[1].ObjectName=="Restored target",error);
        Check(editor.ReadObjectTarget(replacement->GetInstanceID(),sourceTarget,error),error);
        editor.OnActiveSceneChanged();
        Check(!editor.SetField(scope,"FieldsBehaviour::Target",snapshot.Fields[1].Value,error)&&
              !editor.GetSceneObjectChoices(scope,choices,error),"A stale session command addressed the new world");
        Check(editor.ReadFields(owner->GetInstanceID(),1,snapshot,error)&&snapshot.Target.SessionID!=scope.SessionID,error);
        Check(sourceTarget.WorldID==snapshot.Target.WorldID&&
              !editor.ReferenceFromObject(snapshot.Target,sourceTarget,value,error),
              "A stale drag source with reused world/object IDs was accepted by the new field scope");
        Check(editor.ReadObjectTarget(replacement->GetInstanceID(),sourceTarget,error)&&
              editor.ReferenceFromObject(snapshot.Target,sourceTarget,value,error),error);
        CheckRecursiveFieldEditing(editor,scenes,selection,mode,runtime,owner);
        scenes.Playing=true;mode.State.State=EditorPlayState::Playing;
        Check(runtime->BeginExecution(error),error);
        auto number=snapshot.Fields[0].Value;number.Number=3;
        scenes.Apply={};scenes.Revert={};
        Check(editor.SetField(snapshot.Target,"FieldsBehaviour::Speed",number,error)&&!scenes.CanUndo(),"Play field edit changed authoring history");
        auto* native=owner->GetComponent<FieldsBehaviour>();Check(native&&native->Speed==3,"Play setter did not reach native instance");
        native->Speed=11;
        Check(editor.ReadFields(owner->GetInstanceID(),1,snapshot,error)&&snapshot.Fields[0].Value.Number==11,
            "Field snapshot used stale authored values instead of live native fields");
        Check(editor.ObserveFields(snapshot.Target,&listener),"Observe new field session");
        const auto observed=listener.Changes;native->Speed=12;runtime->Update(.016f);
        Check(listener.Changes>observed,"Observed native field change did not emit a scoped notification");
        auto* transient=scenes.Scene->CreateGameObject("Runtime only");const auto transientID=transient->GetInstanceID();
        Check(editor.GetSceneObjectChoices(snapshot.Target,choices,error),error);
        bool foundTransient=false;for(const auto& choice:choices)if(choice.RuntimeObjectID==transientID)
            foundTransient=choice.ObjectFileID==0&&choice.RuntimeSceneID==scenes.Scene->GetInstanceID();
        Check(foundTransient,"Runtime-only node missing from object choices");
        Check(editor.ReadObjectTarget(transientID,sourceTarget,error)&&editor.ReferenceFromObject(snapshot.Target,sourceTarget,value,error),error);
        Check(value.SceneObject.Assigned&&!value.SceneObject.ObjectFileID&&value.SceneObject.RuntimeObjectID==transientID,
            "Transient reference invented a document FileID");
        Check(editor.SetField(snapshot.Target,"FieldsBehaviour::Target",value,error)&&native->Target==transient&&
            editor.LocateObjectReference(snapshot.Target,value,error)&&selection.SceneSelection.ObjectID==transientID,error);
        mode.State.State=EditorPlayState::ExitingPlay;
        Check(!editor.SetField(snapshot.Target,"FieldsBehaviour::Speed",number,error),"ExitingPlay accepted field edits");
        mode.State.State=EditorPlayState::Paused;host.Frame=true;
        Check(!editor.SetField(snapshot.Target,"FieldsBehaviour::Speed",number,error),"Host frame accepted script-field reentry");host.Frame=false;
        Check(editor.SetField(snapshot.Target,"FieldsBehaviour::Speed",number,error),error);
        Check(scenes.Scene->DestroyGameObject(transient),"Delete temporary reference target");
        Check(!editor.SetField(snapshot.Target,"FieldsBehaviour::Target",value,error),"Deleted temporary target accepted");
        Check(runtime->EndExecution(error),error);
        const int previous=listener.Changes;editor.UnregisterScriptListener(&listener);editor.OnSceneStateChanged();
        Check(listener.Changes==previous,"Released listener received a field event");
        editor._Launched=false;editor._Runtime=nullptr;editor._Scenes=nullptr;editor._Selection=nullptr;editor._Editing=nullptr;
        delete runtime;runtime=nullptr;
        std::cout<<"PASS ScriptEditor field values, scene references, transaction, events and stale scopes\n";
        return 0;
    } catch(const std::exception& exception) {std::cerr<<exception.what()<<'\n';delete runtime;return 1;}
}
