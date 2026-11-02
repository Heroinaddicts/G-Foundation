#include "PlayMode.h"
#include "SceneManager.h"
#include "RuntimeHost.h"
#include "AnimationPreview.h"
#include "IProject.h"
#include "GameSproutRuntimeExport.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
#include <stdexcept>

namespace GameSproutEditor {
class GameSproutEditorFinal {
public:
    static void Before(IGameSproutModule& module,GameSproutModuleApi& api) {module.BeforeInitialize(&api);}
    static bool Initialize(IGameSproutModule& module,GameSproutModuleApi& api) {return module.Initialize(&api);}
    static bool Launch(IGameSproutModule& module,GameSproutModuleApi& api) {return module.Launch(&api);}
    static void Tick(IGameSproutModule& module,GameSproutModuleApi& api) {module.Update(&api);}
    static void Release(IGameSproutModule& module,GameSproutModuleApi& api) {module.Release(&api);}
};
}
static void Check(bool value,const std::string& message) {if(!value) throw std::runtime_error(message);}
static void Pump() {for(int index=0;index<8;++index) QCoreApplication::processEvents();}
static QByteArray Bytes(const QString& path) {QFile file(path);Check(file.open(QIODevice::ReadOnly),"Read scene");return file.readAll();}

class Project final : public IProject {
public:
    std::string Root;
    const std::string& GetProjectRoot() const override {return Root;}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override {return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override {return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override {}
};
class Scripts final : public IScriptEditor {
public:
    ScriptBuildState State;
    IScriptEditorListener* Listener=nullptr;
    bool Fail=false,Hold=false;
    int Requests=0;
    std::vector<EditorScriptType> GetScriptTypes() const override {return {};}
    ScriptBuildState GetBuildState() const override {return State;}
    bool RegisterScriptListener(IScriptEditorListener* listener) override {Listener=listener;return true;}
    void UnregisterScriptListener(IScriptEditorListener* listener) override {if(listener==Listener)Listener=nullptr;}
    bool CreateScript(const std::string&,const std::string&,std::string&,std::string&) override {return false;}
    bool RequestCompile(std::string& error) override {
        error.clear();++Requests;State.Compiling=true;State.PendingChanges=true;State.Error.clear();
        if(Listener) Listener->OnScriptBuildStateChanged(State);
        if(!Hold) QTimer::singleShot(0,[this]{Complete();});
        return true;
    }
    void Complete() {
        State.Compiling=false;State.PendingChanges=false;
        State.Error=Fail?"intentional compilation failure":"";
        State.HasSuccessfulBuild=!Fail;
        if(Listener) Listener->OnScriptBuildStateChanged(State);
    }
    bool SetReloadBlocked(bool blocked,std::string& error) override {
        error.clear();if(blocked&&State.PendingChanges) {error="pending sources";return false;}
        State.ReloadBlocked=blocked;return true;
    }
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override {return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override {return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override {Listener=nullptr;}
};
class Editor final : public GameSproutEditor::GameSproutViewApi {
public:
    Editor(const char* runtimeDirectory,const std::string& root):GameSproutViewApi(Export(runtimeDirectory)) {ProjectValue.Root=root;}
    ~Editor() override {delete _Runtime;}
    Project ProjectValue;
    Scripts ScriptsValue;
    RuntimeHost Host;
    SceneManager Scenes;
    AnimationPreview Preview;
    PlayMode Play;
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name) const override {
        if(name=="Project")return const_cast<Project*>(&ProjectValue);
        if(name=="ScriptEditor")return const_cast<Scripts*>(&ScriptsValue);
        if(name=="RuntimeHost")return const_cast<RuntimeHost*>(&Host);
        if(name=="SceneManager")return const_cast<SceneManager*>(&Scenes);
        if(name=="AnimationPreview")return const_cast<AnimationPreview*>(&Preview);
        if(name=="PlayMode")return const_cast<PlayMode*>(&Play);
        return nullptr;
    }
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string&) const override {return nullptr;}
};
class Services {
public:
    explicit Services(Editor& editor):Value(editor),Api(nullptr,&editor) {}
    void Start() {
        for(auto* module:Modules())GameSproutEditor::GameSproutEditorFinal::Before(*module,Api);
        for(auto* module:Modules())Check(GameSproutEditor::GameSproutEditorFinal::Initialize(*module,Api),"Initialize module");
        for(auto* module:Modules())Check(GameSproutEditor::GameSproutEditorFinal::Launch(*module,Api),"Launch module");
    }
    void Tick() {
        GameSproutEditor::GameSproutEditorFinal::Tick(Value.Host,Api);
        GameSproutEditor::GameSproutEditorFinal::Tick(Value.Scenes,Api);
    }
    ~Services() {
        GameSproutEditor::GameSproutEditorFinal::Release(Value.Play,Api);
        GameSproutEditor::GameSproutEditorFinal::Release(Value.Preview,Api);
        GameSproutEditor::GameSproutEditorFinal::Release(Value.Scenes,Api);
        GameSproutEditor::GameSproutEditorFinal::Release(Value.Host,Api);
    }
private:
    std::vector<GameSproutEditor::IGameSproutModule*> Modules() {return {&Value.Host,&Value.Scenes,&Value.Preview,&Value.Play};}
    Editor& Value;
    GameSproutEditor::GameSproutModuleApi Api;
};
class Mover final : public GameSproutRuntime::IScript {
public:
    explicit Mover(GameSproutRuntime::IGameObject* owner):IScript(owner) {}
    static inline int Creates=0,Initializes=0,Updates=0,Fixed=0,Releases=0;
    static inline bool ThrowOnCreate=false;
    const std::type_info& GetComponentType() const override {return typeid(Mover);}
    const char* GetTypeName() const override {return "Test.Mover";}
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override {++Creates;if(ThrowOnCreate)throw std::runtime_error("startup failure");}
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime*) override {++Initializes;}
    void Update(GameSproutRuntime::IGameSproutRuntime*,float delta) override {
        ++Updates;auto* transform=_GameObject->GetComponent<GameSproutRuntime::ITransformComponent>();
        auto position=transform->GetLocalPosition();position.X+=delta;transform->SetLocalPosition(position);
    }
    void FixedUpdate(GameSproutRuntime::IGameSproutRuntime*,float) override {++Fixed;}
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override {++Releases;}
};
static GameSproutRuntime::IGameObject* Find(GameSproutRuntime::IScene* scene,const std::string& name) {
    for(std::size_t index=0;index<scene->GetChildCount();++index)if(scene->GetChild(index)->GetName()==name)return scene->GetChild(index);
    throw std::runtime_error("Missing object: "+name);
}
class Listener final : public IPlayModeListener {
public:
    PlayMode* Owner=nullptr;
    int Changes=0;
    bool ReentryRejected=false;
    void OnPlayModeStateChanged(const PlayModeState&) override {
        ++Changes;std::string error;ReentryRejected=!Owner->Play(error);
    }
};
class FrameListener final : public IRuntimeFrameListener {
public:
    PlayMode* Mode=nullptr;
    bool Rejected=false;
    void OnRuntimeFrame(const RuntimeFrameEvent&) override {
        std::string error;
        const auto state=Mode->GetState().State;
        Rejected=!Mode->Pause(error)&&!Mode->Stop(error)&&Mode->GetState().State==state;
    }
};

int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    try {
        Check(argc==2,"Pass Runtime directory");QTemporaryDir temporary;Check(temporary.isValid(),"Temporary project");
        Check(QDir(temporary.path()).mkpath("Assets/Scenes"),"Create project");
        Editor editor(argv[1],temporary.path().toStdString());Check(editor._Runtime,"Load Runtime");Services services(editor);services.Start();
        auto* runtime=editor._Runtime;std::string error;
        GameSproutRuntime::ScriptTypeRegistration type;
        type.Info.TypeID="test.mover";type.Info.Name="Mover";type.CppType=&typeid(Mover);
        type.Source={"1234567890abcdef1234567890abcdef",1};
        type.Create=[](GameSproutRuntime::IGameObject* object)->GameSproutRuntime::IScript* {return new Mover(object);};
        type.Destroy=[](GameSproutRuntime::IScript* value){delete value;};
        Check(runtime->RegisterScriptType(type,error),error);
        auto* author=editor.Scenes.GetActiveScene();
        auto* object=author->CreateGameObject("Mover");Check(object,"Create authored object");
        Check(runtime->AddScriptComponent(object,type.Info.TypeID,type.Source,1,error),error);
        Check(editor.Scenes.SaveSceneAs("Scenes/Test.scene",error),error);
        const QString sceneFile=temporary.path()+"/Assets/Scenes/Test.scene";const QByteArray saved=Bytes(sceneFile);
        const auto fileID=editor.Scenes.GetObjectFileID(object->GetInstanceID());Check(fileID,"Stable object file ID");
        Check(editor.Scenes.EditObject(object->GetInstanceID(),"Unsaved position",
            [](GameSproutRuntime::IGameObject& item,std::string&){item.GetComponent<GameSproutRuntime::ITransformComponent>()->SetLocalPosition({3,0,0});return true;},
            [](GameSproutRuntime::IGameObject& item,std::string&){item.GetComponent<GameSproutRuntime::ITransformComponent>()->SetLocalPosition({0,0,0});return true;},error),error);
        Check(editor.Scenes.HasUnsavedChanges()&&editor.Scenes.CanUndo(),"Unsaved authored state");
        Listener listener;listener.Owner=&editor.Play;Check(editor.Play.RegisterPlayModeListener(&listener),"Subscribe mode");
        Check(editor.Play.Play(error),error);Check(editor.Play.GetState().State==EditorPlayState::EnteringPlay,"Async preparation state");
        Pump();Check(editor.Play.GetState().State==EditorPlayState::Playing,editor.Play.GetState().Error);
        Check(runtime==editor._Runtime&&editor.Scenes.IsPlaySessionActive()&&editor.Host.IsGameplayActive(),"Execution session not activated");
        Check(editor.ScriptsValue.State.ReloadBlocked&&Mover::Creates==1&&Mover::Initializes==1,"Startup barriers or reload lease");
        FrameListener frameListener;frameListener.Mode=&editor.Play;
        Check(editor.Host.RegisterFrameListener(&frameListener),"Subscribe frame");
        auto* played=editor.Scenes.QueryObjectByFileID(fileID);Check(played&&played!=object,"World was not reconstructed");
        QThread::msleep(25);services.Tick();Check(Mover::Updates>0&&Mover::Fixed>0,"Gameplay/fixed frame dispatch");
        Check(frameListener.Rejected,"A frame observer changed mode or destroyed its own world");
        editor.Host.UnregisterFrameListener(&frameListener);
        Check(!editor.Scenes.SaveScene(error)&&Bytes(sceneFile)==saved,"Play must not save runtime state");
        Check(!editor.Scenes.Undo(error),"Play mutated author undo history");
        Check(editor.Play.Pause(error),error);const int updates=Mover::Updates,fixed=Mover::Fixed,creates=Mover::Creates;
        QThread::msleep(20);services.Tick();Check(Mover::Updates==updates&&Mover::Fixed==fixed&&Mover::Creates==creates&&Mover::Releases==0,"Pause changed instances or advanced gameplay");
        Check(editor.Play.Step(error),error);services.Tick();
        Check(Mover::Updates==updates+1&&Mover::Fixed==fixed+1&&editor.Play.GetState().State==EditorPlayState::Paused,"Step not exactly one paused frame");
        services.Tick();Check(Mover::Updates==updates+1,"Step kept ticking");
        Check(editor.Play.Resume(error),error);
        auto* extra=runtime->CreateScene("Runtime extra");Check(extra&&extra->CreateGameObject("Temporary"),"Runtime creation");
        Check(played->GetScene()->DestroyGameObject(played),"Runtime deletion");
        Check(editor.Play.Stop(error),error);
        Check(editor.Play.GetState().State==EditorPlayState::Edit&&!editor.Scenes.IsPlaySessionActive()&&!runtime->IsScriptExecutionEnabled(),"Stop did not leave execution");
        Check(runtime->GetSceneCount()==1&&Mover::Releases==1&&!editor.ScriptsValue.State.ReloadBlocked,"Runtime world or module lease leaked");
        auto* restored=editor.Scenes.QueryObjectByFileID(fileID);
        Check(restored&&restored->GetComponent<GameSproutRuntime::ITransformComponent>()->GetLocalPosition().X==3,"Unsaved authored position lost");
        Check(editor.Scenes.HasUnsavedChanges()&&editor.Scenes.CanUndo()&&Bytes(sceneFile)==saved,"Document metadata changed");
        Check(editor.Scenes.Undo(error),error);Check(editor.Scenes.QueryObjectByFileID(fileID)->GetComponent<GameSproutRuntime::ITransformComponent>()->GetLocalPosition().X==0,"Author undo no longer resolves stable ID");
        Check(editor.Scenes.Redo(error),error);
        Check(listener.Changes>=6&&listener.ReentryRejected,"State notifications or reentry protection");
        editor.ScriptsValue.Fail=true;Check(editor.Play.Play(error),error);Pump();
        Check(editor.Play.GetState().State==EditorPlayState::Edit&&!editor.Play.GetState().Error.empty()&&Mover::Creates==1,"Compile failure started game");
        editor.ScriptsValue.Fail=false;editor.ScriptsValue.Hold=true;
        Check(editor.Play.Play(error),error);Check(editor.Play.Stop(error),error);editor.ScriptsValue.Complete();Pump();
        Check(editor.Play.GetState().State==EditorPlayState::Edit&&Mover::Creates==1,"Cancelled entering resumed from a late completion");
        editor.ScriptsValue.Hold=false;Mover::ThrowOnCreate=true;
        Check(editor.Play.Play(error),error);Pump();
        Check(editor.Play.GetState().State==EditorPlayState::Edit&&!editor.Play.GetState().Error.empty()&&!editor.Scenes.IsPlaySessionActive(),"Startup failure did not restore author world");
        Mover::ThrowOnCreate=false;Check(editor.Play.Play(error),error);Pump();
        Check(editor.Play.GetState().State==EditorPlayState::Playing,editor.Play.GetState().Error);
        Check(editor.Play.Stop(error),error);editor.Play.UnregisterPlayModeListener(&listener);
        std::cout<<"PASS Play/Stop: unsaved scene, undo, lifecycle, fixed frames, pause/step, failures, cancellation, stable Runtime and disk protection\n";
    } catch(const std::exception& exception) {std::cerr<<exception.what()<<'\n';return 1;}
}
