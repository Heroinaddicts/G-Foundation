#include "GameSproutRuntimeExport.h"
#include "RuntimeResourceApi.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

static const std::string Guid="0123456789abcdef0123456789abcdef";
static void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
static bool Near(float a,float b){return std::fabs(a-b)<.002f;}
class Resources final:public GameSproutRuntime::IRuntimeResourceProvider {
public:
    std::uint64_t Revision=1;int ClipLoads=0,ControllerLoads=0;bool InvalidClip=false;
    std::uint64_t GetRevision()const override{return Revision;}
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData&,std::string&)override{return false;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MeshRenderData&,std::string&)override{return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData&,std::string&)override{return false;}
    bool LoadSkeleton(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::SkeletonData& data,std::string&)override{data={};data.Nodes={{"/bone","Bone",-1,{}}};return true;}
    bool LoadAnimationClip(const GameSproutRuntime::ResourceReference& reference,GameSproutRuntime::AnimationClipData& data,std::string& error)override{
        ++ClipLoads;if(reference.LocalID==99){error="Missing Clip";return false;}
        data={};data.Duration=1;data.SampleRate=1;data.Tracks={InvalidClip?"/missing":"/bone"};
        const float offset=float(reference.LocalID-1)*20;
        GameSproutRuntime::AnimationFrame a,b;a.Time=0;b.Time=1;a.Poses.resize(1);b.Poses.resize(1);
        a.Poses[0].Position.X=offset;b.Poses[0].Position.X=offset+4;data.Frames={a,b};data.Events={{.25f,"Event","must not dispatch"}};return true;
    }
    bool LoadAnimatorController(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::AnimatorControllerData& data,std::string&)override{
        ++ControllerLoads;data={};data.Parameters={{"Speed",GameSproutRuntime::AnimatorParameterType::Float,0},{"Go",GameSproutRuntime::AnimatorParameterType::Trigger,0}};
        GameSproutRuntime::AnimatorLayer layer;layer.Name="Base";layer.IKPass=true;layer.States={{"Idle",{Guid,1}},{"Other",{Guid,3}}};
        GameSproutRuntime::AnimatorTransition transition;transition.From=-1;transition.To=0;transition.Duration=0;transition.Conditions={{"Speed",GameSproutRuntime::AnimatorCompare::Less,1}};layer.Transitions={transition};data.Layers={layer};return true;
    }
};
class Listener final:public GameSproutRuntime::IRuntimeListener {
public:
    int Changes=0,Animations=0,IK=0,Root=0;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event)override{if(event.Type==GameSproutRuntime::SceneNodeEventType::ComponentChanged)++Changes;}
    void OnAnimationEvent(const GameSproutRuntime::AnimationNotification&)override{++Animations;}
    void OnAnimatorIK(const GameSproutRuntime::AnimatorIKNotification&)override{++IK;}
    bool OnRootMotion(const GameSproutRuntime::RootMotionNotification&)override{++Root;return false;}
};
struct Character {GameSproutRuntime::IGameObject* Root;GameSproutRuntime::IGameObject* Bone;GameSproutRuntime::IAnimatorComponent* Animator;};
static Character Make(GameSproutRuntime::IScene& scene,bool controller){
    Character result;result.Root=scene.CreateGameObject("Model");result.Bone=scene.CreateGameObject("Bone",result.Root);result.Bone->GetTransform()->SetLocalPosition({7,0,0});
    result.Animator=result.Root->AddComponent<GameSproutRuntime::IAnimatorComponent>();result.Animator->SetSkeleton({Guid,10});
    if(controller)result.Animator->SetController({Guid,11});
    Check(result.Animator->SetNodeBindings({{"/bone",result.Bone->GetInstanceID()}}),"Cannot bind fixture bone");return result;
}
static float Position(const Character& character){return character.Bone->GetTransform()->GetWorldMatrix()(0,3);}

int main(int argc,char** argv){
    if(argc!=2)return 2;auto* runtime=Export(argv[1]);if(!runtime)return 3;
    try{
        Resources resources;runtime->SetResourceProvider(&resources);auto* scene=runtime->CreateScene("Direct clip");auto model=Make(*scene,true);auto* animator=model.Animator;
        Check(animator->Play("Idle",.5f)&&animator->SetTrigger("Go"),animator->GetLastError());runtime->Update(0);animator->Pause();
        const auto original=animator->GetPlaybackState();const auto controller=animator->GetController(),defaultClip=animator->GetDefaultClip();
        Listener listener;runtime->RegisterListener(&listener);
        Check(animator->PlayClip({Guid,2},.25f),animator->GetLastError());runtime->Update(.25f);
        Check(Near(Position(model),22),"AnyState transition overrode direct Clip");
        auto playback=animator->GetClipPlaybackState();Check(playback.Active&&playback.Playing&&Near(playback.Time,.5f),"Direct Clip clock did not advance");
        const auto frozen=animator->GetPlaybackState();double parameter=0;
        Check(frozen.CurrentState==original.CurrentState&&Near(frozen.Time,original.Time),"Direct Clip changed Controller state/clock");
        Check(animator->GetParameter("Speed",parameter)&&parameter==0&&animator->GetParameter("Go",parameter)&&parameter==1,"Direct Clip changed parameters or consumed trigger");
        Check(animator->SetClipPlaybackPlaying(false)&&animator->SeekClip(.8f),animator->GetLastError());runtime->Update(.5f);
        Check(Near(Position(model),23.2f)&&Near(animator->GetClipPlaybackState().Time,.8f),"Pause or seek failed");
        Check(!animator->PlayClip({Guid,99})&&!animator->PlayClip({Guid,3},-1)&&!animator->SeekClip(-1),"Invalid Clip input accepted");
        Check(Near(Position(model),23.2f)&&animator->GetClipPlaybackState().Clip.LocalID==2,"Failed switch destroyed valid Clip preview");
        Check(animator->PlayClip({Guid,3},.9f,2,false),animator->GetLastError());runtime->Update(.2f);
        Check(Near(Position(model),44)&&!animator->GetClipPlaybackState().Playing&&Near(animator->GetClipPlaybackState().Time,1),"Non-loop endpoint/speed failed");
        Check(animator->PlayClip({Guid,2},.9f,2,true),animator->GetLastError());runtime->Update(.2f);Check(Near(Position(model),21.2f),"Loop wrap/speed failed");
        Check(animator->GetController()==controller&&animator->GetDefaultClip()==defaultClip&&animator->GetLoop()&&Near(animator->GetSpeed(),1),"Direct Clip changed authored settings");
        Check(listener.Changes==0&&listener.Animations==0&&listener.IK==0&&listener.Root==0,"Direct Clip emitted authoring/gameplay callbacks");
        animator->StopClip();runtime->Update(.5f);Check(!animator->GetClipPlaybackState().Active&&Near(Position(model),7),"StopClip failed to restore authoring pose and freeze Controller");
        animator->Resume();runtime->Update(0);Check(!animator->GetClipPlaybackState().Active&&Near(Position(model),2),"Controller cannot resume after Clip playback");animator->Stop();
        const int controllerLoads=resources.ControllerLoads;auto standalone=Make(*scene,false);
        Check(standalone.Animator->PlayClip({Guid,2},.25f),standalone.Animator->GetLastError());runtime->Update(.25f);
        Check(Near(Position(standalone),22)&&resources.ControllerLoads==controllerLoads,"Clip-only playback required Controller or DefaultClip");
        const int clipLoads=resources.ClipLoads;++resources.Revision;runtime->Update(.1f);
        Check(standalone.Animator->GetClipPlaybackState().Active&&resources.ClipLoads>clipLoads,"Hot reload did not revalidate direct Clip resources");
        Check(animator->Play("Idle"),animator->GetLastError());Check(animator->PlayClip({Guid,2}),animator->GetLastError());
        ++resources.Revision;runtime->Update(.1f);animator->Stop();Check(!animator->GetClipPlaybackState().Active&&Near(Position(model),7),"Stop after direct resource reload retained stale Controller/pose data");
        standalone.Animator->SetDefaultClip({Guid,1});Check(!standalone.Animator->GetClipPlaybackState().Active&&!standalone.Bone->GetTransform()->IsAnimationDriven(),"Configuration replacement retained temporary pose");
        Check(standalone.Animator->PlayClip({Guid,2}),standalone.Animator->GetLastError());Check(scene->DestroyGameObject(standalone.Bone),"Cannot delete fixture bone");runtime->Update(.1f);
        Check(!standalone.Animator->GetClipPlaybackState().Active,"Deleting a bound bone did not stop direct playback");
        Check(animator->PlayClip({Guid,2}),animator->GetLastError());resources.InvalidClip=true;++resources.Revision;runtime->Update(.1f);
        Check(!animator->GetClipPlaybackState().Active&&!animator->GetLastError().empty()&&Near(Position(model),7),"Invalid reimport left a stale direct pose");
        runtime->UnregisterListener(&listener);runtime->SetResourceProvider(nullptr);delete runtime;std::cout<<"Direct Clip playback isolation, switching, seeking and lifecycle checks passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';delete runtime;return 1;}
}
