#include "RuntimeHost.h"
#include "GameSproutRuntimeExport.h"
#include "Internal/RuntimeScriptBindings.h"
#include <QCoreApplication>
#include <QThread>
#include <iostream>
#include <stdexcept>

namespace GameSproutEditor {
class GameSproutEditorFinal {
public:
    static bool Start(IGameSproutModule& module,GameSproutModuleApi& api){module.BeforeInitialize(&api);return module.Initialize(&api)&&module.Launch(&api);}
    static void Tick(IGameSproutModule& module,GameSproutModuleApi& api){module.Update(&api);}
    static void Stop(IGameSproutModule& module,GameSproutModuleApi& api){module.Release(&api);}
};
}
namespace {
void Check(bool value,const std::string& error){if(!value)throw std::runtime_error(error);}
int Created=0,Released=0,Early=0,Updated=0,Late=0,Fixed=0,EarlyPress=0,UpdatePress=0,LatePress=0,FixedPress=0;
bool FixedSawEarly=true;std::string Order;
class Mover final : public GameSproutRuntime::ScriptBehaviour {
public:
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override {++Created;}
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override {++Released;}
    void EarlyUpdate(GameSproutRuntime::IGameSproutRuntime* runtime,float) override {++Early;Order+='E';if(runtime->GetInputState().GetKey(GameSproutRuntime::InputKey::W).Pressed)++EarlyPress;}
    void Update(GameSproutRuntime::IGameSproutRuntime* runtime,float dt) override {++Updated;Order+='U';if(runtime->GetInputState().GetKey(GameSproutRuntime::InputKey::W).Pressed)++UpdatePress;auto p=_GameObject->GetTransform()->GetLocalPosition();p.X+=dt;_GameObject->GetTransform()->SetLocalPosition(p);}
    void LateUpdate(GameSproutRuntime::IGameSproutRuntime* runtime,float) override {++Late;Order+='L';if(runtime->GetInputState().GetKey(GameSproutRuntime::InputKey::W).Pressed)++LatePress;auto p=_GameObject->GetTransform()->GetLocalPosition();p.Y=p.X;_GameObject->GetTransform()->SetLocalPosition(p);}
    void FixedUpdate(GameSproutRuntime::IGameSproutRuntime* runtime,float) override {++Fixed;Order+='F';FixedSawEarly=FixedSawEarly&&Early==Updated+1;if(runtime->GetInputState().GetKey(GameSproutRuntime::InputKey::W).Pressed)++FixedPress;}
};
class Editor final : public GameSproutEditor::GameSproutViewApi {
public:
    explicit Editor(GameSproutRuntime::IGameSproutRuntime* runtime):GameSproutViewApi(runtime){}
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string&)const override{return nullptr;}
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string&)const override{return nullptr;}
};
class Frames final : public IRuntimeFrameListener {
public:
    RuntimeHost* Host=nullptr;int Count=0;RuntimeFrameEvent Last;bool ReentryRejected=false,FrameBoundaryReported=false,TransitionBoundaryReported=false,LifecycleComplete=true;
    GameSproutRuntime::IGameSproutRuntime* Runtime=nullptr;
    std::uint64_t TemporaryID=0;
    void OnRuntimeFrame(const RuntimeFrameEvent& value)override{
        ++Count;Last=value;std::string error;FrameBoundaryReported=Host->IsFrameActive();
        if(value.GameplayActive&&value.DeltaSeconds>0)LifecycleComplete=LifecycleComplete&&Early==Updated&&Late==Updated;
        const bool active=Host->IsGameplayActive(),paused=Host->IsGameplayPaused();
        Host->SetGameplayPaused(!paused);Host->EndGameplay();
        ReentryRejected=!Host->StepGameplay(error)&&active==Host->IsGameplayActive()&&paused==Host->IsGameplayPaused();
        // Simulate a first viewport using a temporary object during this frame.
        auto* temporary=Runtime?Runtime->CreateGameObject("First viewport temporary"):nullptr;
        TemporaryID=temporary?temporary->GetInstanceID():0;
    }
    void OnSimulationPlayingChanged(bool) override {
        const bool active=Host->IsGameplayActive(),paused=Host->IsGameplayPaused();std::string error;
        Host->SetGameplayPaused(!paused);Host->EndGameplay();
        TransitionBoundaryReported=Host->IsFrameActive()&&!Host->BeginGameplay(error)&&active==Host->IsGameplayActive()&&paused==Host->IsGameplayPaused();
    }
};
class LaterView final : public IRuntimeFrameListener {
public:
    GameSproutRuntime::IGameSproutRuntime* Runtime=nullptr;
    Frames* First=nullptr;
    bool SawTemporaryAlive=false;
    void OnRuntimeFrame(const RuntimeFrameEvent&) override {
        SawTemporaryAlive=Runtime&&First&&First->TemporaryID&&Runtime->QueryGameObject(First->TemporaryID);
    }
};
void CheckFrameTail(const Frames& first,const LaterView& later,GameSproutRuntime::IGameSproutRuntime& runtime) {
    Check(first.TemporaryID&&later.SawTemporaryAlive,"Temporary object was reclaimed between two viewport frame listeners");
    Check(!runtime.QueryGameObject(first.TemporaryID),"RuntimeHost did not collect the temporary object after all viewport listeners returned");
}
}
int main(int argc,char** argv){
    QCoreApplication application(argc,argv);if(argc!=2)return 2;
    auto* runtime=Export(argv[1]);
    try{
        Check(runtime,"Cannot load Runtime");
        Editor editor(runtime);GameSproutEditor::GameSproutModuleApi api(nullptr,&editor);RuntimeHost host;
        Check(GameSproutEditor::GameSproutEditorFinal::Start(host,api),"Cannot initialize RuntimeHost");
        Frames frames;frames.Host=&host;frames.Runtime=runtime;Check(host.RegisterFrameListener(&frames),"Cannot subscribe frames");
        LaterView later;later.Runtime=runtime;later.First=&frames;Check(host.RegisterFrameListener(&later),"Cannot subscribe second viewport frame");
        GameSproutRuntime::Internal::RegisterScriptClass<Mover>("host-mover","Mover",__FILE__,{"host-source",1});
        const auto registration=GameSproutRuntime::Internal::ScriptModuleTypes().front();std::string error;
        Check(runtime->RegisterScriptType(registration,error),error);auto* object=runtime->CreateScene("Game")->CreateGameObject("Mover");
        Check(runtime->AddScriptComponent(object,registration.Info.TypeID,registration.Source,0,error),error);
        host.SetSimulationPlaying(true);GameSproutEditor::GameSproutEditorFinal::Tick(host,api);
        CheckFrameTail(frames,later,*runtime);
        Check(Created==0&&Early==0&&Updated==0&&Late==0,"Edit preview started native scripts");host.SetSimulationPlaying(false);
        Check(!host.StepGameplay(error),"Single step accepted in Edit");Check(host.BeginGameplay(error),error);
        Check(Created==1&&host.IsGameplayActive()&&runtime->IsScriptExecutionEnabled(),"Gameplay did not start script lifecycle");
        Check(frames.TransitionBoundaryReported&&!host.IsFrameActive(),"Host transition callbacks allowed recursive begin/pause/stop");
        GameSproutRuntime::RuntimeInputEvent input;input.Type=GameSproutRuntime::InputEventType::FocusGained;
        Check(runtime->SubmitInputEvent(input),"Game input focus rejected");input.Type=GameSproutRuntime::InputEventType::KeyDown;input.Key=GameSproutRuntime::InputKey::W;
        Check(runtime->SubmitInputEvent(input),"Game key rejected");
        QThread::msleep(40);GameSproutEditor::GameSproutEditorFinal::Tick(host,api);
        CheckFrameTail(frames,later,*runtime);
        Check(Early==1&&Updated==1&&Late==1&&Fixed>=1&&Fixed<=6&&Order=="E"+std::string(Fixed,'F')+"UL"&&FixedSawEarly,"Host did not run Early, fixed steps, Update and Late in order");
        Check(EarlyPress==1&&UpdatePress==1&&LatePress==1&&FixedPress==Fixed&&runtime->GetInputState().GetKey(GameSproutRuntime::InputKey::W).Held&&!runtime->GetInputState().GetKey(GameSproutRuntime::InputKey::W).Pressed,"Input was consumed before the complete frame including LateUpdate");
        Check(frames.LifecycleComplete&&object->GetTransform()->GetLocalPosition().Y==object->GetTransform()->GetLocalPosition().X,"Viewport frame was notified before LateUpdate completed");
        Check(frames.FrameBoundaryReported&&frames.ReentryRejected&&!host.IsFrameActive(),"Frame callbacks allowed recursive pause/stop or hid their boundary");
        host.SetGameplayPaused(true);const int oldUpdated=Updated,oldFixed=Fixed,oldFrames=frames.Count;const auto position=object->GetTransform()->GetLocalPosition();
        Check(!runtime->GetInputState().GetKey(GameSproutRuntime::InputKey::W).Held&&!runtime->GetInputState().Focused&&!runtime->SubmitInputEvent(input),"Pause retained or accepted gameplay input");
        host.SetSimulationPlaying(true);Check(host.IsGameplayPaused()&&!host.IsSimulationPlaying(),"Animation preview overrode paused gameplay");
        QThread::msleep(20);GameSproutEditor::GameSproutEditorFinal::Tick(host,api);
        CheckFrameTail(frames,later,*runtime);
        Check(Early==oldUpdated&&Updated==oldUpdated&&Late==oldUpdated&&Fixed==oldFixed&&Released==0&&Created==1&&runtime->IsScriptExecutionEnabled(),"Pause advanced or restarted native state");
        Check(frames.Count==oldFrames+1&&frames.Last.GameplayPaused&&frames.Last.DeltaSeconds==0&&frames.ReentryRejected,"Pause stopped rendering events or allowed frame reentry");
        Check(object->GetTransform()->GetLocalPosition().X==position.X,"Paused simulation changed transform");
        Order.clear();Check(host.StepGameplay(error),error);Check(!host.StepGameplay(error),"Duplicate queued step accepted");
        Check(Updated==oldUpdated,"Step executed synchronously instead of next Host tick");
        GameSproutEditor::GameSproutEditorFinal::Tick(host,api);
        Check(Early==oldUpdated+1&&Updated==oldUpdated+1&&Late==oldUpdated+1&&Fixed==oldFixed+1&&Order=="EFUL"&&host.IsGameplayPaused()&&Released==0,"Single step did not advance exactly one complete paused frame");
        Check(EarlyPress==1&&UpdatePress==1&&LatePress==1&&FixedPress==oldFixed&&!runtime->SubmitInputEvent(input),"Single step repeated an old key edge or enabled paused input");
        GameSproutEditor::GameSproutEditorFinal::Tick(host,api);Check(Updated==oldUpdated+1,"Single step accidentally resumed continuous simulation");
        host.SetGameplayPaused(false);QThread::msleep(20);GameSproutEditor::GameSproutEditorFinal::Tick(host,api);
        Check(Updated==oldUpdated+2&&Created==1,"Resume recreated native instances");
        host.EndGameplay();Check(!host.IsGameplayActive()&&!runtime->IsScriptExecutionEnabled()&&Released==1,"Stop leaked native session");
        Check(host.BeginGameplay(error)&&Created==2,"Second Play did not create fresh native instances");host.EndGameplay();Check(Released==2,"Second stop leaked native session");
        host.UnregisterFrameListener(&later);host.UnregisterFrameListener(&frames);GameSproutEditor::GameSproutEditorFinal::Stop(host,api);delete runtime;runtime=nullptr;
        std::cout<<"PASS RuntimeHost: unique full-frame clock, Early/fixed/Update/Late order, complete viewport notification, pause, queued step, input and two sessions\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';delete runtime;return 1;}
}
