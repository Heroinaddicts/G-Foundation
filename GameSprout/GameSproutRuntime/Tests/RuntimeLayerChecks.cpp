#include "GameSproutRuntimeExport.h"
#include "RuntimeResourceApi.h"
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>

static const std::string Guid="0123456789abcdef0123456789abcdef";
static void Check(bool value,const std::string& text){if(!value)throw std::runtime_error(text);}
static bool Near(float a,float b){return std::fabs(a-b)<.003f;}
class Resources final : public GameSproutRuntime::IRuntimeResourceProvider {
public:
    GameSproutRuntime::AnimatorControllerData Controller;
    GameSproutRuntime::AvatarMaskData Mask;
    std::uint64_t Revision=1;int MaskLoads=0,ControllerLoads=0;
    bool MissingHand=false,WrongTrack=false,ZeroReferenceScale=false,AnimatedReference=false,AnimatedBase=false,SparseByClip=false;
    Resources(){
        Controller.Parameters={{"Go",GameSproutRuntime::AnimatorParameterType::Trigger,0}};
        GameSproutRuntime::AnimatorLayer base;base.Name="Base";base.States={{"Idle",{Guid,1}},{"Go",{Guid,4}}};
        GameSproutRuntime::AnimatorLayer upper;upper.Name="Upper";upper.DefaultWeight=.5f;upper.Mask={Guid,20};upper.States={{"Idle",{Guid,2}},{"Go",{Guid,4}}};
        Controller.Layers={base,upper};Mask.Skeleton={Guid,10};Mask.Nodes={{"/arm",1}};
    }
    std::uint64_t GetRevision()const override{return Revision;}
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData&,std::string&)override{return false;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MeshRenderData&,std::string&)override{return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData&,std::string&)override{return false;}
    bool LoadSkeleton(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::SkeletonData& data,std::string&)override{
        data.Nodes={{"/hand","Hand",1,{{2,0,0},{},{1,1,1}}},{"/arm","Arm",2,{{1,0,0},{},{2,2,2}}},{"/root","Root",-1,{}},{"/leg","Leg",2,{}}};return true;
    }
    bool LoadAnimatorController(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::AnimatorControllerData& data,std::string&)override{++ControllerLoads;data=Controller;return true;}
    bool LoadAvatarMask(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::AvatarMaskData& data,std::string&)override{++MaskLoads;data=Mask;return true;}
    bool LoadAnimationClip(const GameSproutRuntime::ResourceReference& ref,GameSproutRuntime::AnimationClipData& data,std::string&)override{
        data={};data.Duration=1;data.SampleRate=1;
        const bool base=ref.LocalID==1,reference=ref.LocalID==3,motion=ref.LocalID==5||ref.LocalID==6;
        data.Tracks=base||motion?std::vector<std::string>{"/hand","/arm","/root","/leg"}:std::vector<std::string>{"/arm","/hand"};
        if(!base&&!motion&&(MissingHand||(SparseByClip&&ref.LocalID==2)))data.Tracks.pop_back();if(WrongTrack)data.Tracks[0]="/foreign";
        for(int time=0;time<2;++time){GameSproutRuntime::AnimationFrame frame;frame.Time=float(time);
            for(const auto& track:data.Tracks){GameSproutRuntime::TransformPose pose;
                if(track=="/arm"){pose.Position.X=base?2:reference?(10+(AnimatedReference?time*2:0)):ref.LocalID==4?22:12;pose.Scale={base?3.f:reference?2.f:4.f,base?3.f:reference?2.f:4.f,base?3.f:reference?2.f:4.f};
                    if(!base){const float angle=(reference?45.f:90.f)*GameSproutRuntime::Pi/360;pose.Rotation={0,0,std::sin(angle),std::cos(angle)};}
                    if(reference&&ZeroReferenceScale)pose.Scale.X=0;
                }else if(track=="/hand")pose.Position.X=base?(3+(AnimatedBase?time*10:0)):reference?11:ref.LocalID==4?23:13;
                else if(track=="/leg")pose.Position.X=4+(base&&AnimatedBase?time*10:0);
                else if(track=="/root")pose.Position.X=motion?time*(ref.LocalID==5?1.f:3.f):1;
                frame.Poses.push_back(pose);
            }data.Frames.push_back(frame);
        }
        if(motion){data.RootMotionNodePath="/root";data.RootMotion={{{0,0,0},{}},{{ref.LocalID==5?1.f:3.f,0,0},{}}};}
        data.Events={{.25f,"Marker","payload"}};return true;
    }
};
class Listener final : public GameSproutRuntime::IRuntimeListener {
public:
    int Changes=0,RootCalls=0;bool Solve=false;GameSproutRuntime::IAnimatorComponent* Animator=nullptr;
    std::vector<GameSproutRuntime::AnimationNotification> Events;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event)override{if(event.Type==GameSproutRuntime::SceneNodeEventType::ComponentChanged)++Changes;}
    void OnAnimationEvent(const GameSproutRuntime::AnimationNotification& event)override{Events.push_back(event);}
    bool OnRootMotion(const GameSproutRuntime::RootMotionNotification&)override{++RootCalls;if(Solve)Check(Animator->ApplyRootMotionDelta({{.1f,0,0},{}}),"Cannot submit layer-blended solved motion");return false;}
};
struct Character {GameSproutRuntime::IGameObject *Object,*Arm,*Hand,*Leg;GameSproutRuntime::IAnimatorComponent* Animator;};
static Character Make(GameSproutRuntime::IScene& scene){
    auto* object=scene.CreateGameObject("Character");auto* root=scene.CreateGameObject("Root",object);auto* arm=scene.CreateGameObject("Arm",root);auto* hand=scene.CreateGameObject("Hand",arm);auto* leg=scene.CreateGameObject("Leg",root);
    auto* animator=object->AddComponent<GameSproutRuntime::IAnimatorComponent>();animator->SetSkeleton({Guid,10});animator->SetController({Guid,11});
    animator->SetNodeBindings({{"/root",root->GetInstanceID()},{"/arm",arm->GetInstanceID()},{"/hand",hand->GetInstanceID()},{"/leg",leg->GetInstanceID()}});return {object,arm,hand,leg,animator};
}
int main(int argc,char** argv){
    if(argc!=2)return 2;auto* runtime=Export(argv[1]);if(!runtime)return 3;Resources resources;runtime->SetResourceProvider(&resources);
    try{
        auto* scene=runtime->CreateScene("Layers");auto first=Make(*scene),second=Make(*scene);auto* animator=first.Animator;
        Listener listener;listener.Animator=animator;runtime->RegisterListener(&listener);
        Check(animator->Play("Idle"),animator->GetLastError());Check(second.Animator->Play("Idle"),second.Animator->GetLastError());
        Check(animator->GetLayerCount()==2&&animator->GetLayerName(1)=="Upper","Layer names/count missing");
        Check(Near(first.Arm->GetTransform()->GetLocalMatrix()(0,3),7)&&Near(first.Hand->GetTransform()->GetLocalMatrix()(0,3),8)&&Near(first.Leg->GetTransform()->GetLocalMatrix()(0,3),4),"Override mask failed Parent inheritance or lower-body preservation");
        Check(resources.MaskLoads==1&&resources.ControllerLoads==1,"Immutable masks/controllers were not shared");
        Check(!animator->SetLayerWeight(0,1)&&!animator->SetLayerWeight(1,-1)&&!animator->SetLayerWeight(1,2)&&!animator->Play("Idle",0,2),"Invalid layer API input accepted");
        runtime->Update(0);listener.Changes=0;Check(animator->SetLayerWeight(1,0),"Cannot mute upper layer");
        Check(Near(first.Arm->GetTransform()->GetLocalMatrix()(0,3),2)&&Near(second.Arm->GetTransform()->GetLocalMatrix()(0,3),7),"Layer weights leak across Animator instances");
        Check(animator->SetLayerWeight(1,.5f)&&animator->Seek(.1f,0)&&animator->Seek(.8f,1),"Layer seek failed");
        Check(Near(animator->GetPlaybackState().Time,.1f)&&Near(animator->GetPlaybackState(1).Time,.8f),"Layer clocks are shared");
        Check(listener.Changes==0,"Layer preview marked authoring scene dirty");
        Check(animator->Play("Idle",0,1)&&animator->CrossFade("Go",.4f,0,1),"Upper CrossFade failed");runtime->Update(.2f);
        Check(animator->GetPlaybackState().NextState.empty()&&animator->GetPlaybackState(1).NextState=="Go"&&Near(animator->GetPlaybackState(1).TransitionWeight,.5f),"Layer transitions share state");
        animator->Stop();second.Animator->Stop();
        resources.Mask.Nodes={{"/arm",0},{"/hand",1}};++resources.Revision;Check(animator->Play("Idle"),animator->GetLastError());
        Check(Near(first.Arm->GetTransform()->GetLocalMatrix()(0,3),2)&&Near(first.Hand->GetTransform()->GetLocalMatrix()(0,3),8),"Mask reload or child override failed");
        resources.Mask.Nodes={{"/arm",1}};resources.Controller.Layers[1].BlendingMode=GameSproutRuntime::AnimatorLayerBlendingMode::Additive;++resources.Revision;
        Check(animator->Play("Idle"),animator->GetLastError());
        Check(Near(first.Arm->GetTransform()->GetLocalMatrix()(0,3),7.5f)&&Near(first.Arm->GetTransform()->GetLocalMatrix()(0,0),4.5f*std::sqrt(.5f)),"Rest-relative additive position/rotation/scale failed");
        resources.Controller.Layers[1].ReferenceClip={Guid,3};++resources.Revision;Check(animator->Play("Idle"),animator->GetLastError());
        Check(Near(first.Arm->GetTransform()->GetLocalMatrix()(0,3),3)&&Near(first.Arm->GetTransform()->GetLocalMatrix()(0,0),4.5f*std::cos(GameSproutRuntime::Pi/8)),"Clip-reference additive delta failed");
        resources.AnimatedReference=true;resources.Controller.Layers[1].ReferenceTime=.5f;++resources.Revision;Check(animator->Play("Idle"),animator->GetLastError());
        Check(Near(first.Arm->GetTransform()->GetLocalMatrix()(0,3),2.5f),"Additive ReferenceTime was not sampled in seconds");resources.AnimatedReference=false;resources.Controller.Layers[1].ReferenceTime=0;
        resources.Controller.Layers[1].ReferenceClip={Guid,2};++resources.Revision;Check(animator->Play("Idle"),animator->GetLastError());
        Check(Near(first.Arm->GetTransform()->GetLocalMatrix()(0,3),2)&&Near(first.Arm->GetTransform()->GetLocalMatrix()(0,0),3),"Additive reference pose must contribute identity");
        resources.Controller.Layers[1].ReferenceClip={};resources.Controller.Layers[1].BlendingMode=GameSproutRuntime::AnimatorLayerBlendingMode::Override;resources.MissingHand=true;++resources.Revision;
        Check(animator->Play("Idle"),animator->GetLastError());Check(Near(first.Hand->GetTransform()->GetLocalMatrix()(0,3),3),"Missing upper tracks overwrote lower pose with rest pose");resources.MissingHand=false;
        resources.AnimatedBase=true;resources.MissingHand=true;++resources.Revision;
        Check(animator->Play("Idle",0,0)&&animator->Play("Idle",0,1)&&animator->CrossFade("Go",.8f,0,1),"Cannot start sparse upper fade");runtime->Update(.2f);
        Check(animator->CrossFade("Idle",.8f,0,1),"Cannot interrupt sparse upper fade");runtime->Update(.2f);
        Check(Near(first.Hand->GetTransform()->GetLocalMatrix()(0,3),7),"Interrupted Override froze an unwritten bone's previous lower pose");
        Check(animator->CrossFade("Go",.8f,0,1),"Cannot interrupt upper fade twice");runtime->Update(.2f);
        Check(Near(first.Hand->GetTransform()->GetLocalMatrix()(0,3),9),"Repeated upper interruption froze missing-track lower animation");
        resources.MissingHand=false;resources.SparseByClip=true;++resources.Revision;
        Check(animator->Play("Idle",0,0)&&animator->Play("Idle",0,1)&&animator->CrossFade("Go",.8f,0,1),"Cannot start partially written upper fade");runtime->Update(.2f);
        Check(animator->CrossFade("Idle",.8f,0,1),"Cannot freeze partial upper coverage");runtime->Update(.2f);
        Check(Near(first.Hand->GetTransform()->GetLocalMatrix()(0,3),8.5f),"Interrupted sparse Override baked the missing fraction of lower pose");
        resources.Controller.Parameters.push_back({"Blend",GameSproutRuntime::AnimatorParameterType::Float,.25});
        GameSproutRuntime::AnimatorState sparseBlend;sparseBlend.Name="SparseBlend";sparseBlend.BlendParameter="Blend";sparseBlend.Motions={{{Guid,2},0,1},{{Guid,4},1,1}};
        resources.Controller.Layers[1].States.push_back(sparseBlend);++resources.Revision;
        Check(animator->Play("Idle",0,0)&&animator->Play("SparseBlend",0,1)&&animator->CrossFade("Go",.8f,0,1),"Cannot fade a sparse 1D state");runtime->Update(.2f);
        Check(animator->CrossFade("Idle",.8f,0,1),"Cannot freeze a sparse 1D state");runtime->Update(.2f);
        Check(Near(first.Hand->GetTransform()->GetLocalMatrix()(0,3),9.625f),"Interrupted 1D blend lost continuous per-bone write coverage");
        resources.Controller.Layers[1].BlendingMode=GameSproutRuntime::AnimatorLayerBlendingMode::Additive;++resources.Revision;
        Check(animator->Play("Idle",0,0)&&animator->Play("Idle",0,1)&&animator->CrossFade("Go",.8f,0,1),"Cannot start additive partial fade");runtime->Update(.2f);
        Check(animator->CrossFade("Idle",.8f,0,1),"Cannot freeze additive upper contribution");runtime->Update(.2f);
        Check(Near(first.Hand->GetTransform()->GetLocalMatrix()(0,3),8.96875f),"Interrupted Additive lost reference-relative write coverage");
        resources.AnimatedBase=false;resources.SparseByClip=false;resources.Controller.Layers[1].BlendingMode=GameSproutRuntime::AnimatorLayerBlendingMode::Override;++resources.Revision;
        for(auto& layer:resources.Controller.Layers){GameSproutRuntime::AnimatorTransition transition;transition.From=0;transition.To=1;transition.Duration=0;transition.Conditions={{"Go",GameSproutRuntime::AnimatorCompare::If,0}};layer.Transitions={transition};}
        ++resources.Revision;Check(animator->Play("Idle")&&animator->SetTrigger("Go"),"Cannot configure shared trigger");runtime->Update(.01f);double trigger=1;
        Check(animator->GetPlaybackState().CurrentState=="Go"&&animator->GetPlaybackState(1).CurrentState=="Go"&&animator->GetParameter("Go",trigger)&&trigger==0,"Trigger consumed before all layers observed it");
        Check(animator->Play("Idle",0,0)&&animator->Play("Idle",0,1),"Cannot reset layers");runtime->Update(0);listener.Events.clear();runtime->Update(.3f);
        bool baseEvent=false,upperEvent=false;for(const auto& event:listener.Events)if(event.Type==GameSproutRuntime::AnimationNotificationType::ClipEvent){baseEvent|=event.LayerIndex==0&&event.LayerName=="Base"&&Near(event.Weight,1);upperEvent|=event.LayerIndex==1&&event.LayerName=="Upper"&&Near(event.Weight,.5f);}
        Check(baseEvent&&upperEvent,"Layer marker identity/weight failed, or root mask incorrectly suppressed upper-body events");
        animator->SetLayerWeight(1,0);animator->Play("Idle",0,0);animator->Play("Idle",0,1);runtime->Update(0);listener.Events.clear();runtime->Update(.3f);
        for(const auto& event:listener.Events)Check(event.Type!=GameSproutRuntime::AnimationNotificationType::ClipEvent||event.LayerIndex==0,"Zero-weight layer emitted clip markers");
        resources.Controller.Layers[0].States[0].Clip={Guid,5};resources.Controller.Layers[1].States[0].Clip={Guid,6};resources.Controller.Layers[0].Transitions.clear();resources.Controller.Layers[1].Transitions.clear();resources.Mask.RootMotionWeight=0;++resources.Revision;
        animator->SetRootMotionMode(GameSproutRuntime::RootMotionMode::ApplyToObject);Check(animator->Play("Idle"),animator->GetLastError());listener.RootCalls=0;runtime->Update(.5f);
        Check(Near(animator->GetRootMotionDelta().Translation.X,.5f)&&listener.RootCalls==1,"Root mask did not preserve base root motion or dispatched per layer");
        resources.Mask.RootMotionWeight=1;++resources.Revision;Check(animator->Play("Idle"),animator->GetLastError());runtime->Update(.5f);
        Check(Near(animator->GetRootMotionDelta().Translation.X,1),"Override root delta was fully added instead of weighted");
        resources.Controller.Layers[1].States[0].Clip={Guid,2};++resources.Revision;Check(animator->Play("Idle"),animator->GetLastError());runtime->Update(.5f);
        Check(Near(animator->GetRootMotionDelta().Translation.X,.5f),"Upper layer without root trajectory overwrote base movement");resources.Controller.Layers[1].States[0].Clip={Guid,6};
        resources.Controller.Layers[1].BlendingMode=GameSproutRuntime::AnimatorLayerBlendingMode::Additive;++resources.Revision;Check(animator->Play("Idle"),animator->GetLastError());listener.Solve=true;runtime->Update(.5f);
        Check(Near(animator->GetRootMotionDelta().Translation.X,1.25f)&&Near(first.Object->GetTransform()->GetWorldMatrix()(0,3),.1f),"Additive root delta or once-only collision solution failed");listener.Solve=false;
        resources.Controller.Layers[1].ReferenceClip={Guid,3};resources.ZeroReferenceScale=true;++resources.Revision;Check(!animator->Play("Idle")&&!animator->GetLastError().empty(),"Singular additive reference accepted");resources.ZeroReferenceScale=false;
        resources.Controller.Layers[1].ReferenceClip={};resources.Mask.Nodes={{"/unknown",1}};++resources.Revision;Check(!animator->Play("Idle"),"Unknown mask path accepted");
        resources.Mask.Nodes={{"/arm",1}};resources.Mask.Skeleton={Guid,99};++resources.Revision;Check(!animator->Play("Idle"),"Mask from a different skeleton accepted");resources.Mask.Skeleton={Guid,10};
        resources.Controller.Layers[0].DefaultWeight=.5f;++resources.Revision;Check(!animator->Play("Idle"),"Invalid base layer weight accepted");resources.Controller.Layers[0].DefaultWeight=1;
        resources.WrongTrack=true;++resources.Revision;Check(!animator->Play("Idle"),"Foreign skeleton clip accepted");
        runtime->UnregisterListener(&listener);runtime->SetResourceProvider(nullptr);delete runtime;
        std::cout<<"Layer isolation, Override/Additive reference poses, masks, markers, triggers and root-motion checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';delete runtime;return 1;}
}
