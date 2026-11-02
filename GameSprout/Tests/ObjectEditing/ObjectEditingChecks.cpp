#include "ObjectEditing.h"
#include "IPlayMode.h"
#include "IRuntimeHost.h"
#include "GameSproutRuntimeExport.h"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace GameSproutEditor {
class GameSproutEditorFinal {
public:
    static void Before(IGameSproutModule& module,GameSproutModuleApi& api){module.BeforeInitialize(&api);}
    static bool Initialize(IGameSproutModule& module,GameSproutModuleApi& api){return module.Initialize(&api);}
    static void Release(IGameSproutModule& module,GameSproutModuleApi& api){module.Release(&api);}
};
}
static void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
class Scenes final:public ISceneManager {
public:
    GameSproutRuntime::IGameSproutRuntime* Runtime=nullptr;
    GameSproutRuntime::IScene* Scene=nullptr;
    bool Playing=false;
    int AuthorWrites=0,LayerWrites=0;
    std::string Path;
    GameSproutRuntime::IScene* GetActiveScene()const override{return Scene;}
    const std::string& GetActiveScenePath()const override{return Path;}
    bool IsPlaySessionActive()const override{return Playing;}
    bool EditObject(uint64_t id,const std::string&,const fSceneEditOperation& apply,const fSceneEditOperation&,std::string& error)override {
        ++AuthorWrites;return apply(*Runtime->QueryGameObject(id),error);
    }
    bool SetObjectLayer(uint64_t,std::uint8_t,bool,std::string&)override{++LayerWrites;return true;}
    bool OpenScene(const std::string&,std::string&)override{return false;}
    bool Undo(std::string&)override{return false;}bool Redo(std::string&)override{return false;}
    bool CanUndo()const override{return false;}bool CanRedo()const override{return false;}
    bool SaveScene(std::string&)override{return false;}bool SaveSceneAs(const std::string&,std::string&)override{return false;}
    bool HasUnsavedChanges()const override{return false;}
    bool CreateSceneAsset(const std::string&,const std::string&,std::string&,std::string&)override{return false;}
    bool RegisterSceneListener(ISceneListener*)override{return true;}void UnregisterSceneListener(ISceneListener*)override{}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
class Mode final:public IPlayMode {
public:
    PlayModeState State;
    PlayModeState GetState()const override{return State;}
    bool Play(std::string&)override{return false;}bool Pause(std::string&)override{return false;}bool Resume(std::string&)override{return false;}
    bool Step(std::string&)override{return false;}bool Stop(std::string&)override{return false;}
    bool RegisterPlayModeListener(IPlayModeListener*)override{return true;}void UnregisterPlayModeListener(IPlayModeListener*)override{}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
class Host final:public IRuntimeHost {
public:
    bool Frame=false;
    bool IsFrameActive()const override{return Frame;}
    void SetSimulationPlaying(bool)override{}bool IsSimulationPlaying()const override{return false;}
    void ResetClock()override{}
    bool RegisterFrameListener(IRuntimeFrameListener*)override{return true;}void UnregisterFrameListener(IRuntimeFrameListener*)override{}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
class Editor final:public GameSproutEditor::GameSproutViewApi {
public:
    explicit Editor(GameSproutRuntime::IGameSproutRuntime* runtime):GameSproutViewApi(runtime){}
    std::map<std::string,GameSproutEditor::IGameSproutModule*> Modules;
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name)const override{const auto found=Modules.find(name);return found==Modules.end()?nullptr:found->second;}
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string&)const override{return nullptr;}
};
class Completion final:public IObjectEditingListener {
public:
    IObjectEditing* Editing=nullptr;
    int Count=0;
    bool ReentryRejected=false;
    void OnObjectPropertiesChanged(const ObjectEditEvent& event)override{
        ++Count;std::string error;ReentryRejected=!Editing->CanEdit(event.Target,error);
    }
};
int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    auto* runtime=argc==2?Export(argv[1]):nullptr;
    if(!runtime){std::cerr<<"Runtime unavailable\n";return 1;}
    try {
        Scenes scenes;scenes.Runtime=runtime;scenes.Scene=runtime->CreateScene("Editing world");
        auto* object=scenes.Scene->CreateGameObject("Object");auto* child=scenes.Scene->CreateGameObject("Child",object);
        Mode mode;Host host;Editor editor(runtime);editor.Modules={{"SceneManager",&scenes},{"PlayMode",&mode},{"RuntimeHost",&host}};
        GameSproutEditor::GameSproutModuleApi api(nullptr,&editor);ObjectEditing editing;
        GameSproutEditor::GameSproutEditorFinal::Before(editing,api);
        Check(GameSproutEditor::GameSproutEditorFinal::Initialize(editing,api),"Cannot initialize property service");
        const ObjectEditTarget target{scenes.Scene->GetInstanceID(),object->GetInstanceID(),object->GetTransform()->GetComponentInstanceID()};
        const auto position=[](float x){return [x](GameSproutRuntime::IGameObject& object,std::string&){object.GetTransform()->SetLocalPosition({x,0,0});return true;};};
        Completion completion;completion.Editing=&editing;Check(editing.RegisterEditingListener(&completion),"Cannot subscribe");
        std::string error;
        Check(editing.EditObject(target,"Author",position(1),position(0),error)&&scenes.AuthorWrites==1&&completion.Count==1&&completion.ReentryRejected,error);
        Check(editing.SetLayer(target,3,true,error)&&scenes.LayerWrites==1,"Edit Layer did not delegate");
        scenes.Playing=true;mode.State.State=EditorPlayState::Playing;
        Check(editing.EditObject(target,"Play",position(2),position(1),error)&&scenes.AuthorWrites==1&&object->GetTransform()->GetLocalPosition().X==2,error);
        const auto failed=[](GameSproutRuntime::IGameObject& object,std::string& reason){object.GetTransform()->SetLocalPosition({7,0,0});reason="rejected";return false;};
        Check(!editing.EditObject(target,"Failure",failed,position(2),error)&&object->GetTransform()->GetLocalPosition().X==2,"Failed setter did not roll back");
        const auto throwing=[](GameSproutRuntime::IGameObject& object,std::string&)->bool{object.GetTransform()->SetLocalPosition({9,0,0});throw std::runtime_error("setter exception");};
        Check(!editing.EditObject(target,"Throw",throwing,position(2),error)&&object->GetTransform()->GetLocalPosition().X==2,"Throwing setter did not roll back");
        const auto before=completion.Count;host.Frame=true;
        Check(!editing.EditObject(target,"Frame",position(3),position(2),error)&&completion.Count==before,"Host frame allowed reentry");host.Frame=false;
        for(const auto state:{EditorPlayState::EnteringPlay,EditorPlayState::ExitingPlay}) {mode.State.State=state;Check(!editing.CanEdit(target,error),"Transition allowed mutation");}
        mode.State.State=EditorPlayState::Playing;
        bool rejected=false;object->ForeachChild([&](GameSproutRuntime::IGameObject*){rejected=!editing.CanEdit(target,error);});Check(rejected,"Runtime callback allowed mutation");
        std::thread other([&]{std::string reason;rejected=!editing.CanEdit(target,reason);});other.join();Check(rejected,"Foreign thread allowed mutation");
        Check(object->SetLayer(1)&&child->SetLayer(2)&&editing.SetLayer(target,5,true,error)&&object->GetLayer()==5&&child->GetLayer()==5&&scenes.LayerWrites==1,"Play subtree wrote author document or omitted child");
        const ObjectEditTarget missingComponent{target.SceneID,target.ObjectID,target.ComponentID+100000};Check(!editing.CanEdit(missingComponent,error),"Missing component accepted");
        auto* mesh=runtime->AddBuiltinComponent(object,"GameSproutRuntime.Mesh",error);Check(mesh,error);
        const ObjectEditTarget removedComponent{target.SceneID,target.ObjectID,mesh->GetComponentInstanceID()};
        const auto removeComponent=[runtime](GameSproutRuntime::IGameObject& node,std::string& reason){return runtime->RemoveBuiltinComponent(&node,"GameSproutRuntime.Mesh",reason);};
        const auto invalidRollback=[](GameSproutRuntime::IGameObject& node,std::string&){node.SetName("Wrong rollback target");return true;};
        Check(!editing.EditObject(removedComponent,"Invalid structural callback",removeComponent,invalidRollback,error)&&object->GetName()=="Object",
            "Destroyed component was accepted or rollback addressed a replacement target");
        auto* doomed=scenes.Scene->CreateGameObject("Destroyed during operation");
        const ObjectEditTarget doomedTarget{target.SceneID,doomed->GetInstanceID(),0};
        const auto destroy=[](GameSproutRuntime::IGameObject& node,std::string&){return node.GetScene()->DestroyGameObject(&node);};
        Check(!editing.EditObject(doomedTarget,"Invalid object deletion",destroy,invalidRollback,error)&&!runtime->QueryGameObject(doomedTarget.ObjectID),
            "Destroyed object was accepted or resurrected during property rollback");
        Check(!editing.CanEdit({target.SceneID+100000,target.ObjectID,0},error),"Stale world accepted");
        const auto id=child->GetInstanceID();Check(scenes.Scene->DestroyGameObject(child),"Cannot remove target");
        Check(!editing.CanEdit({target.SceneID,id,0},error),"Deleted node accepted");
        mode.State.State=EditorPlayState::Paused;
        Check(editing.EditObject(target,"Paused",position(4),position(2),error)&&object->GetTransform()->GetLocalPosition().X==4,error);
        editing.UnregisterEditingListener(&completion);GameSproutEditor::GameSproutEditorFinal::Release(editing,api);
        Check(!editing.CanEdit(target,error),"Released property service kept providers");
        delete runtime;std::cout<<"PASS property mode routing, identities, rollback, subtree values, completion and safe-point guards\n";return 0;
    } catch(const std::exception& exception){delete runtime;std::cerr<<"FAIL "<<exception.what()<<'\n';return 1;}
}
