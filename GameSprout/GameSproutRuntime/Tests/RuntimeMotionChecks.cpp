#include "GameSproutRuntimeExport.h"
#include "RuntimeResourceApi.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

static const std::string Guid="0123456789abcdef0123456789abcdef";
static void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
static bool Near(float a,float b){return std::fabs(a-b)<.003f;}
class Resources final : public GameSproutRuntime::IRuntimeResourceProvider {
public:
    std::uint64_t Revision=1;bool BadMotion=false,BoundaryEvents=false,LongSecondClip=false;
    std::uint64_t GetRevision()const override{return Revision;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MeshRenderData&,std::string&)override{return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData&,std::string&)override{return false;}
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData&,std::string&)override{return false;}
    bool LoadSkeleton(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::SkeletonData& data,std::string&)override {
        data={};data.Nodes={{"/Root","Root",-1,{}},{"/Root/Bone","Bone",0,{{0,1,0},{},{1,1,1}}}};return true;
    }
    bool LoadAnimationClip(const GameSproutRuntime::ResourceReference& ref,GameSproutRuntime::AnimationClipData& data,std::string&)override {
        data={};data.Duration=LongSecondClip&&ref.LocalID==2?2:1;data.SampleRate=2;data.Tracks={"/Root","/Root/Bone"};data.RootMotionNodePath="/Root";
        const float speed=ref.LocalID==2?3:1;
        for(int i=0;i<3;++i){const float time=i*.5f*data.Duration;GameSproutRuntime::Quaternion rotation;
            if(ref.LocalID==3)rotation={0,std::sin(time*GameSproutRuntime::Pi/4),0,std::cos(time*GameSproutRuntime::Pi/4)};
            GameSproutRuntime::AnimationFrame frame;frame.Time=time;frame.Poses={{{time*speed,0,0},rotation,{1,1,1}},{{0,1,0},{},{1,1,1}}};data.Frames.push_back(frame);data.RootMotion.push_back({{time*speed,0,0},rotation});}
        data.Events=BoundaryEvents?std::vector<GameSproutRuntime::AnimationEventData>{{0,"Start",""},{1,"End",""}}:std::vector<GameSproutRuntime::AnimationEventData>{{.25f,"Foot","left"},{.75f,"Foot","right"}};if(BadMotion)data.RootMotion.pop_back();return true;
    }
    bool LoadAnimatorController(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::AnimatorControllerData& data,std::string&)override {
        data={};data.Layers.resize(1);data.Layers[0].Name="Base Layer";data.Parameters={{"Speed",GameSproutRuntime::AnimatorParameterType::Float,.5},{"Go",GameSproutRuntime::AnimatorParameterType::Trigger,0}};
        for(int i=0;i<3;++i){GameSproutRuntime::AnimatorState state;state.Name=i==0?"Walk":i==1?"Run":"Turn";state.Clip={Guid,std::uint64_t(i+1)};data.Layers[0].States.push_back(state);}
        GameSproutRuntime::AnimatorState blend;blend.Name="Blend";blend.BlendParameter="Speed";blend.Motions={{{Guid,1},0,1},{{Guid,2},1,1}};data.Layers[0].States.push_back(blend);
        GameSproutRuntime::AnimatorTransition go;go.From=0;go.To=1;go.Duration=.2f;go.Conditions={{"Go",GameSproutRuntime::AnimatorCompare::If,0}};data.Layers[0].Transitions.push_back(go);return true;
    }
};
class Listener final : public GameSproutRuntime::IRuntimeListener {
public:
    const std::thread::id Thread=std::this_thread::get_id();int Changes=0,RootCalls=0;bool Handled=false;
    std::vector<GameSproutRuntime::AnimationNotification> Events;
    std::function<void()> RootAction,EventAction;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event)override{if(event.Type==GameSproutRuntime::SceneNodeEventType::ComponentChanged)++Changes;}
    void OnAnimationEvent(const GameSproutRuntime::AnimationNotification& event)override {
        Check(std::this_thread::get_id()==Thread,"Event dispatched on a different thread");Events.push_back(event);if(EventAction){const auto action=EventAction;EventAction={};action();}
    }
    bool OnRootMotion(const GameSproutRuntime::RootMotionNotification&)override {
        Check(std::this_thread::get_id()==Thread,"Root motion dispatched on a different thread");++RootCalls;if(RootAction){const auto action=RootAction;RootAction={};action();}return Handled;
    }
};
struct Character {GameSproutRuntime::IGameObject* Object;GameSproutRuntime::IGameObject* Root;GameSproutRuntime::IAnimatorComponent* Animator;};
static Character Make(GameSproutRuntime::IScene& scene){
    auto* object=scene.CreateGameObject("Character");object->GetTransform()->SetLocalPosition({10,0,0});
    auto* root=scene.CreateGameObject("Root",object);auto* bone=scene.CreateGameObject("Bone",root);
    auto* animator=object->AddComponent<GameSproutRuntime::IAnimatorComponent>();
    animator->SetSkeleton({Guid,10});animator->SetController({Guid,11});animator->SetNodeBindings({{"/Root",root->GetInstanceID()},{"/Root/Bone",bone->GetInstanceID()}});
    return {object,root,animator};
}
int main(int argc,char** argv){
    if(argc!=2)return 2;auto* runtime=Export(argv[1]);if(!runtime)return 3;Resources resources;runtime->SetResourceProvider(&resources);
    try {
        auto* scene=runtime->CreateScene("Motion");auto c=Make(*scene);Listener listener;runtime->RegisterListener(&listener);
        Check(c.Animator->SetRootMotionMode(GameSproutRuntime::RootMotionMode::ApplyToObject),"Cannot enable root motion");
        Check(c.Animator->Play("Walk"),c.Animator->GetLastError());runtime->Update(0);listener.Changes=0;listener.Events.clear();
        runtime->Update(2.4f);
        Check(Near(c.Object->GetTransform()->GetWorldMatrix()(0,3),12.4f)&&Near(c.Root->GetTransform()->GetLocalMatrix()(0,3),0),"Root motion wrapped backwards or was applied twice");
        Check(Near(c.Object->GetTransform()->GetLocalPosition().X,10)&&listener.Changes==0,"Root motion modified authoring state");
        Check(c.Object->GetTransform()->IsAnimationDriven()&&listener.RootCalls==1,"Root motion was not a single runtime overlay");
        Check(listener.Events.size()==5&&listener.Events[4].LoopIndex==2&&listener.Events[0].Payload=="left","Looped event interval detection failed");
        c.Animator->Pause();listener.Events.clear();runtime->Update(1);Check(listener.Events.empty()&&Near(c.Object->GetTransform()->GetWorldMatrix()(0,3),12.4f),"Pause emitted motion or events");
        Check(c.Animator->Seek(.5f),"Seek failed");runtime->Update(0);Check(listener.Events.empty()&&Near(c.Object->GetTransform()->GetWorldMatrix()(0,3),12.4f),"Seek emitted historical movement or markers");
        c.Animator->Stop();Check(Near(c.Object->GetTransform()->GetWorldMatrix()(0,3),10)&&!c.Object->GetTransform()->IsAnimationDriven(),"Stop did not restore source position");
        c.Animator->SetRootMotionMode(GameSproutRuntime::RootMotionMode::ExtractOnly);c.Animator->Play("Walk");runtime->Update(.5f);
        Check(Near(c.Animator->GetRootMotionDelta().Translation.X,.5f)&&Near(c.Object->GetTransform()->GetWorldMatrix()(0,3),10)&&Near(c.Root->GetTransform()->GetLocalMatrix()(0,3),0),"ExtractOnly moved the object or failed stripping");
        c.Animator->SetRootMotionMode(GameSproutRuntime::RootMotionMode::Disabled);c.Animator->Play("Walk");runtime->Update(.5f);
        Check(Near(c.Object->GetTransform()->GetWorldMatrix()(0,3),10)&&Near(c.Root->GetTransform()->GetLocalMatrix()(0,3),.5f),"Disabled changed original animation");
        c.Animator->SetRootMotionMode(GameSproutRuntime::RootMotionMode::ApplyToObject);c.Animator->Play("Walk");listener.Handled=true;runtime->Update(.5f);
        Check(Near(c.Object->GetTransform()->GetWorldMatrix()(0,3),10),"Root motion applied despite listener takeover");listener.Handled=false;
        c.Animator->Stop();c.Animator->Play("Walk");
        Check(!c.Animator->ApplyRootMotionDelta({{.1f,0,0},{}}),"Solved root motion was accepted outside its callback");
        listener.RootAction=[&]{Check(c.Animator->ApplyRootMotionDelta({{.25f,0,0},{}}),"Cannot submit collision-solved delta");Check(!c.Animator->ApplyRootMotionDelta({{.25f,0,0},{}}),"Solved delta was accepted twice");};
        runtime->Update(.5f);Check(Near(c.Object->GetTransform()->GetWorldMatrix()(0,3),10.25f)&&Near(c.Object->GetTransform()->GetLocalPosition().X,10),"Collision solution double-applied original movement or changed authoring TRS");
        c.Animator->Stop();c.Animator->Play("Walk");listener.RootAction=[&]{Check(c.Animator->ApplyRootMotionDelta({{.25f,0,0},{}}),"Cannot submit before pause");c.Animator->Pause();};
        runtime->Update(.5f);Check(Near(c.Object->GetTransform()->GetWorldMatrix()(0,3),10),"Pause inside a callback applied stale solved motion");
        c.Animator->Stop();c.Animator->Play("Turn");runtime->Update(1.5f);
        const auto turned=c.Object->GetTransform()->GetWorldMatrix();Check(Near(turned(0,3),11)&&Near(turned(2,3),-.5f),"Rotation across a loop did not compose SE3 trajectory");
        c.Animator->Stop();c.Animator->Play("Walk");runtime->Update(0);listener.Events.clear();Check(c.Animator->CrossFade("Run",.2f),"Cannot crossfade");runtime->Update(.5f);
        Check(Near(c.Animator->GetRootMotionDelta().Translation.X,1.3f),"Long crossfade step integrated the wrong root velocity");
        int enters=0,exits=0;for(const auto& event:listener.Events){enters+=event.Type==GameSproutRuntime::AnimationNotificationType::StateEnter;exits+=event.Type==GameSproutRuntime::AnimationNotificationType::StateExit;}
        Check(enters==1&&exits==1&&c.Animator->GetPlaybackState().CurrentState=="Run","State transition notifications failed");
        c.Animator->Stop();c.Animator->SetFloat("Speed",.5f);c.Animator->Play("Blend");runtime->Update(.25f);Check(Near(c.Animator->GetRootMotionDelta().Translation.X,.5f),"1D blend did not blend root motion");
        c.Animator->Stop();
        auto coarse=Make(*scene),fine=Make(*scene);
        for(auto* animator:{coarse.Animator,fine.Animator}){animator->SetRootMotionMode(GameSproutRuntime::RootMotionMode::ApplyToObject);animator->Play("Walk");animator->CrossFade("Run",1);}
        fine.Animator->Pause();listener.Events.clear();runtime->Update(1);
        Check(Near(coarse.Object->GetTransform()->GetWorldMatrix()(0,3),12),"One-step fade did not integrate average velocity");
        bool lateSource=false;for(const auto& event:listener.Events)if(event.Type==GameSproutRuntime::AnimationNotificationType::ClipEvent&&event.ObjectID==coarse.Object->GetInstanceID()&&event.State=="Walk"&&Near(event.Time,.75f))lateSource=Near(event.Weight,.25f);
        Check(lateSource,"Last crossfade segment lost a still-visible source marker");coarse.Animator->Pause();fine.Animator->Resume();
        for(int step=0;step<10;++step)runtime->Update(.1f);
        Check(Near(fine.Object->GetTransform()->GetWorldMatrix()(0,3),12),"Root motion depends on frame count for constant-velocity crossfade");
        coarse.Animator->Stop();fine.Animator->Stop();
        resources.BoundaryEvents=true;++resources.Revision;c.Animator->Play("Walk");runtime->Update(0);listener.Events.clear();runtime->Update(1);
        std::vector<GameSproutRuntime::AnimationNotification> boundary;for(const auto& event:listener.Events)if(event.Type==GameSproutRuntime::AnimationNotificationType::ClipEvent&&event.ObjectID==c.Object->GetInstanceID())boundary.push_back(event);
        Check(boundary.size()==3&&boundary[0].Name=="Start"&&boundary[0].LoopIndex==0&&boundary[1].Name=="End"&&boundary[1].LoopIndex==0&&boundary[2].Name=="Start"&&boundary[2].LoopIndex==1,"Loop boundary markers were emitted in the wrong cycle order");
        c.Animator->Stop();resources.BoundaryEvents=false;++resources.Revision;
        resources.LongSecondClip=true;++resources.Revision;c.Animator->SetFloat("Speed",.5f);c.Animator->Play("Blend");runtime->Update(.3f);
        const float phaseBefore=c.Animator->GetPlaybackState().NormalizedTime;Check(Near(phaseBefore,.2f),"Unexpected initial unequal-duration blend phase");
        Check(c.Animator->SetFloat("Speed",1)&&Near(c.Animator->GetPlaybackState().NormalizedTime,phaseBefore),"Blend parameter duration change jumped the playback phase");
        listener.Events.clear();runtime->Update(.1f);Check(Near(c.Animator->GetRootMotionDelta().Translation.X,.3f),"Blend parameter change caused root-motion jump");
        for(const auto& event:listener.Events)Check(event.Type!=GameSproutRuntime::AnimationNotificationType::ClipEvent,"Blend parameter change replayed already passed markers");
        c.Animator->Stop();resources.LongSecondClip=false;++resources.Revision;

        c.Animator->Play("Walk");listener.RootAction=[&]{c.Animator->SetRootMotionMode(GameSproutRuntime::RootMotionMode::ExtractOnly);};runtime->Update(.2f);
        Check(Near(c.Object->GetTransform()->GetWorldMatrix()(0,3),10),"Mode change inside root callback applied stale pending motion");
        c.Animator->Stop();c.Animator->SetRootMotionMode(GameSproutRuntime::RootMotionMode::ApplyToObject);
        auto second=Make(*scene);second.Animator->SetRootMotionMode(GameSproutRuntime::RootMotionMode::ApplyToObject);second.Animator->Play("Walk");
        const auto secondID=second.Object->GetInstanceID();listener.RootAction=[&]{scene->DestroyGameObject(second.Object);};c.Animator->Stop();runtime->Update(.1f);Check(!runtime->QueryGameObject(secondID),"Root callback deletion did not defer safely");
        c.Animator->Play("Walk");listener.EventAction=[&]{c.Object->RemoveComponent<GameSproutRuntime::IAnimatorComponent>();};runtime->Update(.3f);
        Check(!c.Object->GetComponent<GameSproutRuntime::IAnimatorComponent>(),"Animation callback removal failed");
        auto burst=Make(*scene);burst.Animator->Play("Walk");runtime->Update(3000);Check(!burst.Animator->GetPlaybackState().Playing&&burst.Animator->GetLastError().find("4096")!=std::string::npos,"Event burst was silently dropped or unbounded");
        runtime->UnregisterListener(&listener);runtime->SetResourceProvider(nullptr);delete runtime;
        std::cout<<"Root motion, marker/state notifications and callback lifetime checks passed\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';delete runtime;return 1;}
}
