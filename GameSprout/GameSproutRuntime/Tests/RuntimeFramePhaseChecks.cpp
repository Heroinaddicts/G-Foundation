#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Components/AnimatorComponent.h"
#include "Components/ScriptComponent.h"
#include "Internal/RuntimeScriptBindings.h"
#include <functional>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace GameSproutRuntime;
namespace {
void Check(bool value,const char* error){if(!value)throw std::runtime_error(error);}
std::vector<std::string> Calls;
class Probe final:public IComponent {
public:
    explicit Probe(IGameObject* owner):IComponent(owner){}
    const std::type_info& GetComponentType()const override{return typeid(Probe);}
    const char* GetTypeName()const override{return "Tests.FrameProbe";}
    std::function<void(const char*)> Action;
    std::string Label;
    unsigned* Releases=nullptr;
    void EarlyUpdate(IGameSproutRuntime*,float)override{Run("E");}
    void FixedUpdate(IGameSproutRuntime*,float)override{Run("F");}
    void Update(IGameSproutRuntime*,float)override{Run("U");}
    void LateUpdate(IGameSproutRuntime*,float)override{Run("L");}
    void OnRelease(IGameSproutRuntime*)override{if(Releases)++*Releases;}
    void Run(const char* phase){Calls.push_back(Label+phase);if(Action)Action(phase);}
};
class EarlyOnly final:public IComponent {
public:
    explicit EarlyOnly(IGameObject* owner):IComponent(owner){}
    const std::type_info& GetComponentType()const override{return typeid(EarlyOnly);}
    const char* GetTypeName()const override{return "Tests.EarlyOnly";}
    unsigned Count=0;
    void EarlyUpdate(IGameSproutRuntime*,float)override{++Count;}
};
class LateOnly final:public IComponent {
public:
    explicit LateOnly(IGameObject* owner):IComponent(owner){}
    const std::type_info& GetComponentType()const override{return typeid(LateOnly);}
    const char* GetTypeName()const override{return "Tests.LateOnly";}
    unsigned Count=0;
    void LateUpdate(IGameSproutRuntime*,float)override{++Count;}
};
static_assert(Runtime::ComponentTickMask<EarlyOnly>()==Runtime::EarlyTick);
static_assert(Runtime::ComponentTickMask<LateOnly>()==Runtime::LateTick);
static_assert(Runtime::ComponentTickMask<AnimatorComponent>()==0);
static_assert(Runtime::ComponentTickMask<ScriptComponent>()==Runtime::AllTicks);
template<typename T>T* Attach(Runtime& runtime,IGameObject* owner){
    auto& state=dynamic_cast<ObjectAccess*>(owner)->State();
    auto* value=new T(owner);
    GameObjectState::ComponentEntry entry{value,++state.NextComponentSerial};
    entry.Initialized=true;entry.TickMask=Runtime::ComponentTickMask<T>();
    state.Components.push_back(entry);runtime.RegisterComponentTick(state.ID,entry.TickMask);return value;
}
void PhaseOrderAndMembership(){
    Runtime runtime;std::string error;auto* scene=runtime.CreateScene("Phases");
    auto* aObject=scene->CreateGameObject("A");auto* bObject=scene->CreateGameObject("B");
    auto* a=Attach<Probe>(runtime,aObject);a->Label="A";
    auto* b=Attach<Probe>(runtime,bObject);b->Label="B";
    auto* early=Attach<EarlyOnly>(runtime,aObject);auto* late=Attach<LateOnly>(runtime,bObject);
    Calls.clear();runtime.AdvanceFrame(.04f,2,.02f);
    Check(Calls==std::vector<std::string>{"AE","BE","AF","BF","AF","BF","AU","BU","AL","BL"},"Frame phases were interleaved by object or fixed-step count was wrong");
    Check(early->Count==1&&late->Count==1,"Early-only/Late-only component was omitted from registration");
    Calls.clear();a->Action=[&](const char* phase){if(std::string(phase)=="E"){
        runtime.AdvanceFrame(.1f,1,.1f);runtime.Update(.1f);runtime.FixedUpdate(.1f);
    }};runtime.Update(.01f);
    Check(Calls==std::vector<std::string>{"AE","BE","AU","BU","AL","BL"},"Callback reentry advanced the Runtime");
    Calls.clear();runtime.AdvanceFrame(-1,1,.02f);runtime.AdvanceFrame(.1f,1,0);
    runtime.AdvanceFrame(.1f,1,std::numeric_limits<float>::infinity());runtime.AdvanceFrame(std::numeric_limits<float>::quiet_NaN(),0,0);
    Check(Calls.empty(),"Invalid frame arguments partially invoked callbacks");
    a->Action={};bool added=false;LateOnly* newcomer=nullptr;
    a->Action=[&](const char* phase){if(std::string(phase)=="E"&&!added){added=true;newcomer=Attach<LateOnly>(runtime,scene->CreateGameObject("New"));}};
    runtime.AdvanceFrame(.04f,2,.02f);Check(newcomer&&newcomer->Count==0,"A newly added component received only the late half of its first frame");
    runtime.Update(.01f);Check(newcomer->Count==1,"A new component did not enter the following complete frame");
    a->Action={};scene->SetSimulationEnabled(false);Calls.clear();runtime.AdvanceFrame(.01f,2,.02f);
    Check(Calls.empty(),"A simulation-disabled scene received frame callbacks");
}
void RemovalAndFailure(){
    Runtime runtime;auto* scene=runtime.CreateScene("Lifetime");
    auto* aObject=scene->CreateGameObject("A");auto* bObject=scene->CreateGameObject("B");
    auto* a=Attach<Probe>(runtime,aObject);a->Label="A";unsigned releases=0;
    auto* b=Attach<Probe>(runtime,bObject);b->Label="B";b->Releases=&releases;
    a->Action=[&](const char* phase){if(std::string(phase)=="E")Check(bObject->RemoveComponent<Probe>(),"Cannot remove next component in EarlyUpdate");};
    Calls.clear();runtime.AdvanceFrame(.02f,1,.02f);
    Check(releases==1&&Calls==std::vector<std::string>{"AE","AF","AU","AL"},"Early removal left callbacks or a dangling later-phase identity");
    a->Action=[&](const char* phase){if(std::string(phase)=="E")aObject->SetActive(false);};
    Calls.clear();runtime.AdvanceFrame(.02f,1,.02f);Check(Calls==std::vector<std::string>{"AE"},"Inactive object continued into later stages");
    aObject->SetActive(true);a->Action=[](const char* phase){if(std::string(phase)=="E")throw std::runtime_error("early failure");};
    Calls.clear();runtime.AdvanceFrame(.02f,1,.02f);Check(Calls==std::vector<std::string>{"AE"},"Throwing component continued into later stages");
    a->Action=[&](const char* phase){if(std::string(phase)=="E")Check(scene->DestroyGameObject(aObject),"Cannot destroy self");};
    Calls.clear();runtime.AdvanceFrame(.02f,1,.02f);Check(Calls==std::vector<std::string>{"AE"},"Self-destroyed component continued into later stages");
}
class ScriptProbe final:public ScriptBehaviour {
public:
    static inline bool Throw=false,Disable=false;
protected:
    void OnInitialized(IGameSproutRuntime*)override{Calls.push_back("I");}
    void EarlyUpdate(IGameSproutRuntime*,float)override{
        Calls.push_back("E");if(Throw)throw std::runtime_error("script early failure");
        if(Disable)_GameObject->GetComponent<IScriptComponent>()->SetEnabled(false);
    }
    void FixedUpdate(IGameSproutRuntime*,float)override{Calls.push_back("F");}
    void Update(IGameSproutRuntime*,float)override{Calls.push_back("U");}
    void LateUpdate(IGameSproutRuntime*,float)override{Calls.push_back("L");}
    void OnRelease(IGameSproutRuntime*)override{Calls.push_back("R");}
};
void ScriptForwarding(){
    Internal::RegisterScriptClass<ScriptProbe>("phase-script","PhaseScript",__FILE__,{"phase-source",1});
    const auto registration=Internal::ScriptModuleTypes().back();
    for(unsigned mode=0;mode<3;++mode){
        Runtime runtime;std::string error;auto* scene=runtime.CreateScene("Script");auto* owner=scene->CreateGameObject("Owner");
        Check(runtime.RegisterScriptType(registration,error),error.c_str());auto* script=runtime.AddScriptComponent(owner,registration.Info.TypeID,registration.Source,0,error);Check(script,error.c_str());
        Calls.clear();Check(runtime.SetScriptExecutionEnabled(true,error),error.c_str());runtime.Update(0);
        Check(Calls.empty(),"A zero-delta edit refresh started user lifecycle");
        ScriptProbe::Throw=mode==1;ScriptProbe::Disable=mode==2;
        runtime.AdvanceFrame(.04f,2,.02f);
        if(mode==0){
            Check(Calls==std::vector<std::string>{"I","E","F","F","U","L"},"Native Early/Late bridge or OnInitialized ordering is incorrect");
            Calls.clear();runtime.Update(0);Check(Calls.empty(),"Zero-delta refresh ran native Early/Update/Late");
        }else{
            Check(Calls==std::vector<std::string>{"I","E","R"},"Failed/self-disabled native script received callbacks after release");
            if(mode==1)Check(!script->GetScriptError().empty(),"EarlyUpdate exception was swallowed without a script diagnostic");
        }
        Check(runtime.EndExecution(error),error.c_str());
    }
    ScriptProbe::Throw=false;ScriptProbe::Disable=false;
}
class ConstructionScript final:public ScriptBehaviour {
public:
    static inline bool InCallback=false,ReleaseOnStack=false,Throw=false,Balanced=false;
    static inline std::string Phase="E";
protected:
    void EarlyUpdate(IGameSproutRuntime* runtime,float)override{Run(runtime,"E");}
    void Update(IGameSproutRuntime* runtime,float)override{Run(runtime,"U");}
    void LateUpdate(IGameSproutRuntime* runtime,float)override{Run(runtime,"L");}
    void OnRelease(IGameSproutRuntime*)override{ReleaseOnStack=ReleaseOnStack||InCallback;Calls.push_back("R");}
private:
    void Run(IGameSproutRuntime* runtime,const char* phase){
        Calls.push_back(phase);if(Phase!=phase)return;
        struct CallbackScope {CallbackScope(){InCallback=true;}~CallbackScope(){InCallback=false;}} callback;
        std::string error;Check(runtime->BeginConstruction(error),error.c_str());
        Check(runtime->CreateGameObject("Callback-created",_GameObject),"Cannot create within a callback-local construction scope");
        if(Balanced)Check(runtime->EndConstruction(error),error.c_str());
        if(Throw)throw std::runtime_error("callback threw after BeginConstruction");
    }
};
void CallbackConstructionRecovery(){
    Internal::RegisterScriptClass<ConstructionScript>("construction-script","ConstructionScript",__FILE__,{"construction-source",1});
    const auto registration=Internal::ScriptModuleTypes().back();
    for(const auto phase:{"E","L"})for(unsigned mode=0;mode<3;++mode){
        Runtime runtime;std::string error;auto* scene=runtime.CreateScene("Construction safety");auto* owner=scene->CreateGameObject("Script");
        Check(runtime.RegisterScriptType(registration,error),error.c_str());auto* script=runtime.AddScriptComponent(owner,registration.Info.TypeID,registration.Source,0,error);Check(script,error.c_str());
        Check(runtime.BeginExecution(error),error.c_str());Calls.clear();
        ConstructionScript::Phase=phase;ConstructionScript::Throw=mode==0;ConstructionScript::Balanced=mode==2;ConstructionScript::ReleaseOnStack=false;
        auto* peer=Attach<EarlyOnly>(runtime,scene->CreateGameObject("Healthy peer"));
        runtime.Update(.01f);
        Check(!runtime.IsConstructing(),"A component callback permanently leaked its construction scope into the Runtime");
        Check(!ConstructionScript::ReleaseOnStack,"Construction error recovery deleted a script while its method was on the stack");
        if(mode==2)Check(script->GetScriptError().empty()&&Calls==std::vector<std::string>{"E","U","L"},"A correctly paired callback-local construction scope was rejected");
        else Check(!script->GetScriptError().empty()&&Calls.back()=="R","An unbalanced construction scope did not fail and release the script");
        const auto oldCount=peer->Count;runtime.Update(.01f);
        Check(peer->Count==oldCount+1,"Construction callback failure froze later Runtime frames");
        Check(runtime.BeginConstruction(error),error.c_str());runtime.Update(.01f);
        Check(runtime.IsConstructing()&&peer->Count==oldCount+1,"Frame rejection destroyed the host's legitimate outer construction scope");
        Check(runtime.EndConstruction(error),error.c_str());Check(runtime.EndExecution(error),error.c_str());
    }
}
class Resources final:public IRuntimeResourceProvider {
public:
    std::uint64_t GetRevision()const override{return 1;}
    bool LoadTexture(const ResourceReference&,TextureRenderData&,std::string&)override{return false;}
    bool LoadMesh(const ResourceReference&,MeshRenderData&,std::string&)override{return false;}
    bool LoadMaterial(const ResourceReference&,MaterialRenderData&,std::string&)override{return false;}
    bool LoadSkeleton(const ResourceReference&,SkeletonData& result,std::string&)override{result={};result.Nodes={{"/bone","Bone",-1,{}}};return true;}
    bool LoadAnimationClip(const ResourceReference& reference,AnimationClipData& result,std::string&)override{
        result={};result.Duration=1;result.SampleRate=1;result.Tracks={"/bone"};
        for(int index=0;index<2;++index){AnimationFrame frame;frame.Time=float(index);TransformPose pose;pose.Position.X=reference.LocalID==2?10.f+index:float(index);frame.Poses={pose};result.Frames.push_back(frame);}
        result.RootMotionNodePath="/bone";result.RootMotion={{{0,0,0},{}},{{1,0,0},{}}};
        return true;
    }
    bool LoadAnimatorController(const ResourceReference&,AnimatorControllerData& result,std::string&)override{
        result={};result.Parameters={{"Go",AnimatorParameterType::Bool,0}};AnimatorLayer layer;layer.Name="Base";layer.States={{"Idle",{"animation",1}},{"Run",{"animation",2}}};
        AnimatorTransition transition;transition.From=0;transition.To=1;transition.Duration=0;transition.Conditions={{"Go",AnimatorCompare::If,0}};layer.Transitions={transition};result.Layers={layer};return true;
    }
};
class MotionListener final:public IRuntimeListener {
public:
    unsigned Count=0;
    std::vector<std::string> Entered;
    std::function<void(const AnimationNotification&)> AnimationAction;
    void OnSceneNodeEvent(const SceneNodeEvent&)override{}
    void OnAnimationEvent(const AnimationNotification& event)override{if(event.Type==AnimationNotificationType::StateEnter)Entered.push_back(event.State);if(AnimationAction)AnimationAction(event);}
    bool OnRootMotion(const RootMotionNotification&)override{++Count;return false;}
};
class Initializer final:public IComponent {
public:
    explicit Initializer(IGameObject* owner):IComponent(owner){}
    const std::type_info& GetComponentType()const override{return typeid(Initializer);}
    const char* GetTypeName()const override{return "Tests.Initializer";}
    std::function<void()> Action;
    void OnInitialized(IGameSproutRuntime*)override{if(Action)Action();}
};
void AnimationBetweenUpdateAndLate(){
    Resources resources;Runtime runtime;runtime.SetResourceProvider(&resources);auto* scene=runtime.CreateScene("Animation ordering");
    // Create Animator before parameter-writer: object order must not delay it.
    auto* animated=scene->CreateGameObject("Animator first");auto* bone=scene->CreateGameObject("Bone",animated);
    auto* animator=animated->AddComponent<IAnimatorComponent>();animator->SetSkeleton({"animation",10});animator->SetController({"animation",11});
    Check(animator->SetNodeBindings({{"/bone",bone->GetInstanceID()}}),"Cannot bind phase test bone");Check(animator->Play("Idle"),animator->GetLastError().c_str());
    auto* writer=Attach<Probe>(runtime,scene->CreateGameObject("Writer after animator"));writer->Label="W";
    bool observed=false;
    writer->Action=[&](const char* phase){
        if(std::string(phase)=="U")Check(animator->SetBool("Go",true),"Cannot set animation parameter in Update");
        if(std::string(phase)=="L")observed=animator->GetPlaybackState().CurrentState=="Run"&&bone->GetTransform()->GetWorldMatrix()(0,3)>=10;
    };
    runtime.Update(.1f);Check(observed,"LateUpdate did not see the animation parameter and pose produced in this same frame");
    MotionListener listener;Check(runtime.RegisterListener(&listener),"Cannot subscribe root motion stage");
    Check(animator->SetRootMotionMode(RootMotionMode::ApplyToObject),"Cannot enable root motion");
    Check(animator->Play("Idle"),animator->GetLastError().c_str());
    observed=false;writer->Action=[&](const char* phase){if(std::string(phase)=="L")observed=listener.Count==1&&animated->GetTransform()->GetWorldMatrix()(0,3)>0;};
    runtime.Update(.1f);Check(observed,"LateUpdate ran before root-motion notifications and final application");
    // A Play made in Late queues a new state notification after the ordinary
    // animation stage. It must still be delivered before AdvanceFrame returns.
    listener.Entered.clear();bool latePlayed=false;
    writer->Action=[&](const char* phase){if(std::string(phase)=="L"&&!latePlayed){latePlayed=true;Check(animator->Play("Run"),animator->GetLastError().c_str());}};
    runtime.Update(.1f);Check(listener.Entered==std::vector<std::string>{"Run"},"Late Play notification escaped to post-render EndFrame");
    // Tail initialization of a newly added component can Play as well.
    listener.Entered.clear();bool initializedAdded=false;
    writer->Action=[&](const char* phase){if(std::string(phase)=="L"&&!initializedAdded){
        initializedAdded=true;auto* initializer=Attach<Initializer>(runtime,scene->CreateGameObject("Tail initializer"));
        initializer->Action=[&]{Check(animator->Play("Idle"),animator->GetLastError().c_str());};
        auto& state=dynamic_cast<ObjectAccess*>(initializer->GetGameObject())->State();state.Components.back().Initialized=false;
        runtime.QueueComponentInitialization(state,state.Components.back().Serial);
    }};
    runtime.Update(.1f);Check(listener.Entered==std::vector<std::string>{"Idle"},"Tail OnInitialized Play notification escaped to post-render EndFrame");
    // The extra tail pass is a bounded batch. A listener's own new Play waits
    // for the next dispatch, rather than recursively draining without bound.
    listener.Entered.clear();latePlayed=false;
    writer->Action=[&](const char* phase){if(std::string(phase)=="L"&&!latePlayed){latePlayed=true;Check(animator->Play("Run"),animator->GetLastError().c_str());}};
    listener.AnimationAction=[&](const AnimationNotification& event){if(event.Type==AnimationNotificationType::StateEnter&&event.State=="Run")Check(animator->Play("Idle"),animator->GetLastError().c_str());};
    runtime.Update(.1f);Check(listener.Entered==std::vector<std::string>{"Run"},"Tail notification pass recursively drained listener-generated animation work");
    listener.AnimationAction={};runtime.ProcessPendingEvents();Check(listener.Entered==std::vector<std::string>{"Run","Idle"},"Deferred listener-generated animation notification was lost");
    runtime.UnregisterListener(&listener);runtime.SetResourceProvider(nullptr);
}
void ShaderFrameClock(){
    Runtime runtime;std::string error;runtime.CreateScene("Shader clock");
    std::this_thread::sleep_for(std::chrono::milliseconds(3));runtime.AdvanceFrame(0,0,0);
    const auto edit=runtime.GetShaderTime();
    Check(edit.X>0&&edit.Y>0,"Edit preview did not advance shader time with a zero simulation delta");
    const auto secondView=runtime.GetShaderTime();
    Check(secondView.X==edit.X&&secondView.Y==edit.Y,"Reading a second viewport's shader time advanced the shared frame clock");
    Check(runtime.BeginExecution(error),error.c_str());runtime.AdvanceFrame(.125f,2,.02f);
    const auto playing=runtime.GetShaderTime();
    Check(std::fabs(playing.X-edit.X-.125f)<.00001f&&playing.Y==.125f,"Gameplay shader time did not use the host's once-per-frame delta");
    runtime.ProcessPendingEvents();std::this_thread::sleep_for(std::chrono::milliseconds(3));runtime.ProcessPendingEvents();
    Check(runtime.GetShaderTime().X==playing.X&&runtime.GetShaderTime().Y==0,"Paused event dispatch advanced shader time");
    runtime.AdvanceFrame(.01f,0,0);const auto resumed=runtime.GetShaderTime();
    Check(std::fabs(resumed.X-playing.X-.01f)<.00001f,"Resume included wall-clock time spent paused");
    runtime.FixedUpdate(.02f);Check(runtime.GetShaderTime().X==resumed.X,"Explicit fixed stepping advanced the shader clock a second time");
    Check(runtime.EndExecution(error),error.c_str());
}
}
int main(){try{
    PhaseOrderAndMembership();RemovalAndFailure();ScriptForwarding();CallbackConstructionRecovery();AnimationBetweenUpdateAndLate();ShaderFrameClock();
    std::cout<<"PASS Runtime frame stages: global barriers, fixed steps, native bridge, mutation safety and same-frame animation\n";return 0;
}catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';return 1;}}
