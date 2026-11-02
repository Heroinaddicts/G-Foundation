#include "StaticAuthoring.h"
#include "ISceneManager.h"
#include "IPlayMode.h"
#include "GameSproutRuntimeExport.h"
#include <map>
#include <iostream>
#include <stdexcept>
namespace GameSproutEditor {
class GameSproutEditorFinal {
public:
    static void Before(IGameSproutModule& value,GameSproutModuleApi& api){value.BeforeInitialize(&api);}
    static bool Initialize(IGameSproutModule& value,GameSproutModuleApi& api){return value.Initialize(&api);}
    static void Release(IGameSproutModule& value,GameSproutModuleApi& api){value.Release(&api);}
};
}
static void Check(bool value,const std::string& reason){if(!value)throw std::runtime_error(reason);}
class Scenes final : public ISceneManager {
public:
    GameSproutRuntime::IScene* Scene=nullptr;std::string Path;
    int Writes=0;std::uint64_t LastObject=0;bool LastChildren=false;
    GameSproutRuntime::StaticFlags LastValue{},LastMask{};
    GameSproutRuntime::IScene* GetActiveScene() const override{return Scene;}
    const std::string& GetActiveScenePath() const override{return Path;}
    bool SetObjectStaticFlags(uint64_t id,GameSproutRuntime::StaticFlags value,GameSproutRuntime::StaticFlags mask,bool children,std::string& error) override{
        error.clear();++Writes;LastObject=id;LastValue=value;LastMask=mask;LastChildren=children;return true;
    }
    bool OpenScene(const std::string&,std::string&) override{return false;}
    bool EditObject(uint64_t,const std::string&,const fSceneEditOperation&,const fSceneEditOperation&,std::string&) override{return false;}
    bool Undo(std::string&) override{return false;}bool Redo(std::string&) override{return false;}
    bool CanUndo() const override{return false;}bool CanRedo() const override{return false;}
    bool SaveScene(std::string&) override{return false;}bool SaveSceneAs(const std::string&,std::string&) override{return false;}
    bool HasUnsavedChanges() const override{return false;}
    bool CreateSceneAsset(const std::string&,const std::string&,std::string&,std::string&) override{return false;}
    bool RegisterSceneListener(ISceneListener*) override{return true;}void UnregisterSceneListener(ISceneListener*) override{}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class PlayService final : public IPlayMode {
public:
    PlayModeState State;
    PlayModeState GetState() const override{return State;}
    bool Play(std::string&) override{return false;}bool Pause(std::string&) override{return false;}bool Resume(std::string&) override{return false;}
    bool Step(std::string&) override{return false;}bool Stop(std::string&) override{return false;}
    bool RegisterPlayModeListener(IPlayModeListener*) override{return true;}void UnregisterPlayModeListener(IPlayModeListener*) override{}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class Editor final : public GameSproutEditor::GameSproutViewApi {
public:
    explicit Editor(GameSproutRuntime::IGameSproutRuntime* runtime):GameSproutViewApi(runtime){}
    std::map<std::string,GameSproutEditor::IGameSproutModule*> Modules;
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name) const override{const auto found=Modules.find(name);return found==Modules.end()?nullptr:found->second;}
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string&) const override{return nullptr;}
};
int main() {
    using GameSproutRuntime::StaticFlags;
    auto* runtime=Export(RUNTIME_LIBRARY_DIRECTORY);if(!runtime){std::cerr<<"Cannot load Runtime\n";return 1;}
    try {
        Scenes scenes;scenes.Scene=runtime->CreateScene("Static authoring");
        auto* parent=scenes.Scene->CreateGameObject("Parent");auto* child=scenes.Scene->CreateGameObject("Child",parent);
        PlayService play;Editor editor(runtime);editor.Modules={{"SceneManager",&scenes},{"PlayMode",&play}};
        GameSproutEditor::GameSproutModuleApi api(nullptr,&editor);StaticAuthoring author;
        GameSproutEditor::GameSproutEditorFinal::Before(author,api);Check(GameSproutEditor::GameSproutEditorFinal::Initialize(author,api),"Cannot initialize authoring");
        const auto definitions=author.GetFlagDefinitions();std::uint32_t bits=0;
        Check(definitions.size()==7&&author.GetSupportedFlags()==StaticFlags::None,"Storage was advertised as a working baking system");
        for(const auto& definition:definitions){Check(!definition.Supported&&!definition.Name.empty()&&!definition.Description.empty(),"Incomplete/incorrect capability");bits|=static_cast<std::uint32_t>(definition.Flag);}
        Check(bits==127,"Incomplete static declaration list");std::string error;
        Check(!author.SetFlags(parent->GetInstanceID(),StaticFlags::ContributeGI,StaticFlags::ContributeGI,false,error)&&scenes.Writes==0&&!error.empty(),"Unimplemented optimization was enabled");
        Check(parent->SetStaticFlags(StaticFlags::ContributeGI|StaticFlags::BatchingStatic)&&child->SetStaticFlags(StaticFlags::ReflectionProbeStatic),"Cannot seed stored declarations");
        Check(author.SetFlags(parent->GetInstanceID(),StaticFlags::None,StaticFlags::BatchingStatic,true,error)&&scenes.Writes==1&&
              scenes.LastObject==parent->GetInstanceID()&&scenes.LastMask==StaticFlags::BatchingStatic&&scenes.LastChildren,"Clear did not route through public scene transaction");
        Check(author.SetFlags(parent->GetInstanceID(),StaticFlags::ContributeGI,StaticFlags::ContributeGI,false,error)&&scenes.Writes==2,"Keeping an existing declaration should be a valid no-op");
        Check(!author.SetFlags(parent->GetInstanceID(),StaticFlags::ContributeGI,StaticFlags::ContributeGI,true,error)&&scenes.Writes==2,"Unsupported enable on a descendant changed part of the subtree");
        Check(!author.SetFlags(parent->GetInstanceID(),static_cast<StaticFlags>(128),StaticFlags::Everything,false,error)&&scenes.Writes==2,"Unknown static bits were accepted");
        play.State.State=EditorPlayState::Playing;
        Check(!author.SetFlags(parent->GetInstanceID(),StaticFlags::None,StaticFlags::Everything,false,error)&&scenes.Writes==2,"Play accepted authoring mutation");play.State.State=EditorPlayState::Edit;
        bool rejected=false;parent->ForeachChild([&](GameSproutRuntime::IGameObject*){rejected=!author.SetFlags(parent->GetInstanceID(),StaticFlags::None,StaticFlags::Everything,false,error);});
        Check(rejected&&scenes.Writes==2,"Runtime callback allowed authoring reentrancy");
        GameSproutEditor::GameSproutEditorFinal::Release(author,api);
        Check(!author.SetFlags(parent->GetInstanceID(),StaticFlags::None,StaticFlags::Everything,false,error),"Released service retained providers");
        delete runtime;std::cout<<"PASS static authoring capabilities, unsupported enable, clear, subtree preflight, Play and callback guards\n";return 0;
    } catch(const std::exception& exception){delete runtime;std::cerr<<"FAIL "<<exception.what()<<"\n";return 1;}
}
