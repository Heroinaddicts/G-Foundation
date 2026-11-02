#include "ScriptEditor.h"
#include "GameSproutRuntimeExport.h"
#include "Internal/RuntimeScriptBindings.h"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>

static void Check(bool value,const std::string& error) {if(!value)throw std::runtime_error(error);}
class FieldsBehaviour final:public GameSproutRuntime::ScriptBehaviour {
public:
    float Speed=1;
    GameSproutRuntime::IGameObject* Target=nullptr;
};
class SceneService final:public ISceneManager {
public:
    GameSproutRuntime::IScene* Scene=nullptr;
    bool Playing=false;
    fSceneEditOperation Apply,Revert;
    uint64_t EditedID=0;
    std::function<void()> Changed;
    std::string Path;
    GameSproutRuntime::IScene* GetActiveScene()const override{return Scene;}
    const std::string& GetActiveScenePath()const override{return Path;}
    bool OpenScene(const std::string&,std::string&)override{return false;}
    bool IsPlaySessionActive()const override{return Playing;}
    bool EditObject(uint64_t id,const std::string&,const fSceneEditOperation& apply,const fSceneEditOperation& revert,std::string& error)override {
        GameSproutRuntime::IGameObject* found=nullptr;
        Scene->ForeachChild([&](auto* child){if(child->GetInstanceID()==id)found=child;});
        if(!found)return false;
        if(!apply(*found,error))return false;
        Apply=apply;Revert=revert;EditedID=id;if(Changed)Changed();return true;
    }
    bool Undo(std::string& error)override {return Revert&&Revert(*Owner(),error);}
    bool Redo(std::string& error)override {return Apply&&Apply(*Owner(),error);}
    bool CanUndo()const override{return bool(Revert);}
    bool CanRedo()const override{return bool(Apply);}
    bool SaveScene(std::string&)override{return false;}
    bool SaveSceneAs(const std::string&,std::string&)override{return false;}
    bool HasUnsavedChanges()const override{return false;}
    bool CreateSceneAsset(const std::string&,const std::string&,std::string&,std::string&)override{return false;}
    bool RegisterSceneListener(ISceneListener*)override{return true;}
    void UnregisterSceneListener(ISceneListener*)override{}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
private:
    GameSproutRuntime::IGameObject* Owner()const {
        for(std::size_t i=0;i<Scene->GetChildCount();++i)if(Scene->GetChild(i)->GetInstanceID()==EditedID)return Scene->GetChild(i);
        return nullptr;
    }
};
class SelectionService final:public ISelectionManager {
public:
    SceneObjectSelection SceneSelection;
    std::vector<ProjectAssetSelection> GetAssetSelection()const override{return {};}
    void SetAssetSelection(const std::vector<ProjectAssetSelection>&)override{}
    SceneObjectSelection GetSceneObjectSelection()const override{return SceneSelection;}
    void SetSceneObjectSelection(const SceneObjectSelection& value)override{SceneSelection=value;}
    bool RegisterSelectionListener(ISelectionListener*)override{return true;}
    void UnregisterSelectionListener(ISelectionListener*)override{}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
class Listener final:public IScriptEditorListener {
public:
    int Changes=0;
    void OnScriptFieldsChanged(const ScriptFieldTarget&)override{++Changes;}
};
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
        SceneService scenes;SelectionService selection;ScriptEditor editor;
        scenes.Scene=runtime->CreateScene("Fields");
        auto* owner=scenes.Scene->CreateGameObject("Owner");auto* target=scenes.Scene->CreateGameObject("Target");
        Check(runtime->BindSceneObjectIdentity(scenes.Scene,0,error)&&runtime->BindSceneObjectIdentity(owner,10,error)&&
              runtime->BindSceneObjectIdentity(target,20,error),error);
        auto* script=runtime->AddScriptComponent(owner,registration.Info.TypeID,{registration.Info.TypeID,1},1,error);Check(script,error);
        editor._Runtime=runtime;editor._Scenes=&scenes;editor._Selection=&selection;editor._Launched=true;
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
        scenes.Playing=true;auto number=snapshot.Fields[0].Value;number.Number=3;
        scenes.Apply={};scenes.Revert={};
        Check(editor.SetField(snapshot.Target,"FieldsBehaviour::Speed",number,error)&&!scenes.CanUndo(),"Play field edit changed authoring history");
        const int previous=listener.Changes;editor.UnregisterScriptListener(&listener);editor.OnSceneStateChanged();
        Check(listener.Changes==previous,"Released listener received a field event");
        editor._Launched=false;editor._Runtime=nullptr;editor._Scenes=nullptr;editor._Selection=nullptr;
        delete runtime;runtime=nullptr;
        std::cout<<"PASS ScriptEditor field values, scene references, transaction, events and stale scopes\n";
        return 0;
    } catch(const std::exception& exception) {std::cerr<<exception.what()<<'\n';delete runtime;return 1;}
}
