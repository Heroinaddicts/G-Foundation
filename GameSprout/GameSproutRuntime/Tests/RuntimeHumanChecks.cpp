#include "GameSproutRuntimeExport.h"
#include "RuntimeResourceApi.h"
#include "HumanoidValidation.h"
#include <iostream>
#include <map>
#include <stdexcept>
#include <limits>
static const std::string Guid="00112233445566778899aabbccddeeff";
static void Check(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
static bool Near(float a,float b,float epsilon=.005f){return std::fabs(a-b)<epsilon;}
static GameSproutRuntime::Quaternion Turn(float angle){return {0,0,std::sin(angle/2),std::cos(angle/2)};}
class HumanResources final:public GameSproutRuntime::IRuntimeResourceProvider {
public:
    GameSproutRuntime::SkeletonData Source,Target;
    GameSproutRuntime::HumanoidAvatarData SourceAvatar,TargetAvatar;
    GameSproutRuntime::AnimatorControllerData Controller;
    std::map<GameSproutRuntime::HumanBone,int> Roles;
    std::uint64_t Revision=1;bool HumanClip=true;
    GameSproutRuntime::AvatarMaskData Mask;
    HumanResources(){
        const auto add=[&](GameSproutRuntime::HumanBone role,int parent,GameSproutRuntime::Vector3 position){
            const int index=int(Source.Nodes.size());Roles[role]=index;Source.Nodes.push_back({"/source/"+std::string(GameSproutRuntime::HumanBoneName(role)),GameSproutRuntime::HumanBoneName(role),parent,{position}});
            SourceAvatar.Bones.push_back({role,Source.Nodes.back().Path,{}});return index;
        };
        const int hips=add(GameSproutRuntime::HumanBone::Hips,-1,{0,1,0});const int spine=add(GameSproutRuntime::HumanBone::Spine,hips,{0,.5f,0});add(GameSproutRuntime::HumanBone::Head,spine,{0,.5f,0});
        for(int side=0;side<2;++side){const float sign=side?1.f:-1.f;
            const auto upperArm=side?GameSproutRuntime::HumanBone::RightUpperArm:GameSproutRuntime::HumanBone::LeftUpperArm;
            const auto lowerArm=side?GameSproutRuntime::HumanBone::RightLowerArm:GameSproutRuntime::HumanBone::LeftLowerArm;
            const auto hand=side?GameSproutRuntime::HumanBone::RightHand:GameSproutRuntime::HumanBone::LeftHand;
            int a=add(upperArm,spine,{sign*.3f,.3f,0});int b=add(lowerArm,a,{sign*.4f,0,0});add(hand,b,{sign*.3f,0,0});
            const auto upperLeg=side?GameSproutRuntime::HumanBone::RightUpperLeg:GameSproutRuntime::HumanBone::LeftUpperLeg;
            const auto lowerLeg=side?GameSproutRuntime::HumanBone::RightLowerLeg:GameSproutRuntime::HumanBone::LeftLowerLeg;
            const auto foot=side?GameSproutRuntime::HumanBone::RightFoot:GameSproutRuntime::HumanBone::LeftFoot;
            a=add(upperLeg,hips,{sign*.15f,-.1f,0});b=add(lowerLeg,a,{0,-.5f,.1f});add(foot,b,{0,-.4f,-.1f});
        }
        const int finger=add(GameSproutRuntime::HumanBone::LeftIndexProximal,Roles.at(GameSproutRuntime::HumanBone::LeftHand),{-.1f,0,0});
        const int middle=add(GameSproutRuntime::HumanBone::LeftIndexIntermediate,finger,{-.1f,0,0});add(GameSproutRuntime::HumanBone::LeftIndexDistal,middle,{-.05f,0,0});
        Target=Source;TargetAvatar=SourceAvatar;SourceAvatar.Skeleton={Guid,1};TargetAvatar.Skeleton={Guid,2};
        for(auto& node:Target.Nodes){node.Path="/different/"+node.Name;node.Name="different_"+node.Name;node.Transform.Position=node.Transform.Position*2;}
        const int leftUpper=Roles.at(GameSproutRuntime::HumanBone::LeftUpperArm),leftLower=Roles.at(GameSproutRuntime::HumanBone::LeftLowerArm);
        Target.Nodes[leftUpper].Transform.Rotation=Turn(.4f);Target.Nodes[leftLower].Transform.Position=GameSproutRuntime::HumanMath::Rotate(Turn(-.4f),Target.Nodes[leftLower].Transform.Position);Target.Nodes[leftLower].Transform.Rotation=Turn(-.4f);
        for(std::size_t i=0;i<SourceAvatar.Bones.size();++i)TargetAvatar.Bones[i].Path=Target.Nodes[i].Path;
        for(const auto& node:Source.Nodes)SourceAvatar.ReferencePose.push_back(node.Transform);
        for(const auto& node:Target.Nodes)TargetAvatar.ReferencePose.push_back(node.Transform);
        std::string error;Check(GameSproutRuntime::BuildHumanAvatarAxes(Source,SourceAvatar,error),error);Check(GameSproutRuntime::BuildHumanAvatarAxes(Target,TargetAvatar,error),error);
        GameSproutRuntime::AnimatorLayer layer;layer.Name="Base";layer.IKPass=true;layer.States={{"Move",{Guid,10}},{"Still",{Guid,11}}};Controller.Layers={layer};
    }
    std::uint64_t GetRevision()const override{return Revision;}
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData&,std::string&)override{return false;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MeshRenderData&,std::string&)override{return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData&,std::string&)override{return false;}
    bool LoadSkeleton(const GameSproutRuntime::ResourceReference& ref,GameSproutRuntime::SkeletonData& data,std::string&)override{data=ref.LocalID==1?Source:Target;return true;}
    bool LoadHumanoidAvatar(const GameSproutRuntime::ResourceReference& ref,GameSproutRuntime::HumanoidAvatarData& data,std::string&)override{data=ref.LocalID==3?SourceAvatar:TargetAvatar;return true;}
    bool LoadAnimatorController(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::AnimatorControllerData& data,std::string&)override{data=Controller;return true;}
    bool LoadAvatarMask(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::AvatarMaskData& data,std::string&)override{data=Mask;return true;}
    bool LoadAnimationClip(const GameSproutRuntime::ResourceReference& ref,GameSproutRuntime::AnimationClipData& data,std::string&)override{
        data={};data.Duration=ref.LocalID==12?2:1;data.SampleRate=1;if(HumanClip)data.SourceAvatar={Guid,3};
        const auto& skeleton=HumanClip?Source:Target;for(const auto& node:skeleton.Nodes)data.Tracks.push_back(node.Path);
        for(int time=0;time<2;++time){GameSproutRuntime::AnimationFrame frame;frame.Time=time*data.Duration;
            for(std::size_t index=0;index<skeleton.Nodes.size();++index){auto pose=skeleton.Nodes[index].Transform;
                if(ref.LocalID==13&&index==std::size_t(Roles.at(GameSproutRuntime::HumanBone::Hips))){pose.Position.X+=time;const float angle=time*.6f;pose.Rotation={0,std::sin(angle/2),0,std::cos(angle/2)};}
                if(time&&ref.LocalID!=11&&ref.LocalID!=13&&index==std::size_t(Roles.at(GameSproutRuntime::HumanBone::LeftIndexProximal)))pose.Rotation=GameSproutRuntime::HumanMath::Product(Turn(.6f),pose.Rotation);
                if(time&&ref.LocalID!=11&&ref.LocalID!=13&&index==std::size_t(Roles.at(GameSproutRuntime::HumanBone::LeftUpperArm)))pose.Rotation=GameSproutRuntime::HumanMath::Product(Turn(.8f),pose.Rotation);
                frame.Poses.push_back(pose);
            }data.Frames.push_back(frame);
        }if(ref.LocalID==13){data.RootMotionNodePath=skeleton.Nodes[Roles.at(GameSproutRuntime::HumanBone::Hips)].Path;data.RootMotion={{{},{}},{{1,0,0},{0,std::sin(.3f),0,std::cos(.3f)}}};}
        data.Events={{data.Duration*.25f,"Marker","human"}};return true;
    }
};
struct Character{GameSproutRuntime::IGameObject* Object;GameSproutRuntime::IAnimatorComponent* Animator;std::vector<GameSproutRuntime::IGameObject*> Nodes;};
static Character Make(GameSproutRuntime::IScene& scene,const HumanResources& resources){
    Character result;result.Object=scene.CreateGameObject("Human");result.Animator=result.Object->AddComponent<GameSproutRuntime::IAnimatorComponent>();
    for(const auto& node:resources.Target.Nodes){auto* object=scene.CreateGameObject(node.Name,node.Parent<0?result.Object:result.Nodes[node.Parent]);object->GetTransform()->SetLocalPosition(node.Transform.Position);object->GetTransform()->SetLocalRotation(node.Transform.Rotation);result.Nodes.push_back(object);}
    result.Animator->SetSkeleton({Guid,2});result.Animator->SetAvatar({Guid,4});result.Animator->SetController({Guid,20});std::vector<GameSproutRuntime::AnimationNodeBinding> bindings;
    for(std::size_t i=0;i<result.Nodes.size();++i)bindings.push_back({resources.Target.Nodes[i].Path,result.Nodes[i]->GetInstanceID()});Check(result.Animator->SetNodeBindings(bindings),"Cannot bind Human fixture");return result;
}
static GameSproutRuntime::Vector3 Position(GameSproutRuntime::IGameObject* object){const auto matrix=object->GetTransform()->GetWorldMatrix();return {matrix(0,3),matrix(1,3),matrix(2,3)};}
class IKListener final:public GameSproutRuntime::IRuntimeListener {
public:
    GameSproutRuntime::IGameSproutRuntime* Runtime=nullptr;Character* Player=nullptr;int Changes=0,Calls=0,RootCalls=0;bool Stop=false,Destroy=false,SolveRoot=false;
    std::vector<GameSproutRuntime::AnimationNotification> Events;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event)override{if(event.Type==GameSproutRuntime::SceneNodeEventType::ComponentChanged)++Changes;}
    void OnAnimationEvent(const GameSproutRuntime::AnimationNotification& event)override{Events.push_back(event);}
    bool OnRootMotion(const GameSproutRuntime::RootMotionNotification& event)override{if(event.ObjectID!=Player->Object->GetInstanceID())return false;++RootCalls;if(SolveRoot){auto delta=event.Delta;delta.Translation=delta.Translation*.25f;Check(Player->Animator->ApplyRootMotionDelta(delta),"Human root solve rejected");}return false;}
    void OnAnimatorIK(const GameSproutRuntime::AnimatorIKNotification& event)override{
        if(event.ObjectID!=Player->Object->GetInstanceID())return;++Calls;
        if(Stop)Player->Animator->Stop();else if(Destroy)Player->Object->GetScene()->DestroyGameObject(Player->Object);
        else{GameSproutRuntime::AnimatorIKGoal goal;goal.Position={-1.f,3.3f,.3f};goal.PositionWeight=1;goal.Hint={-1,4,1};goal.HintWeight=1;Check(Player->Animator->SetIKGoal(GameSproutRuntime::AvatarIKGoal::LeftHand,goal),"Callback IK goal rejected");}
    }
};
int main(int argc,char** argv){if(argc!=2)return 2;auto* runtime=Export(argv[1]);if(!runtime)return 3;
    try{HumanResources resources;runtime->SetResourceProvider(&resources);auto* scene=runtime->CreateScene("Human and IK");auto first=Make(*scene,resources);auto* animator=first.Animator;
        Check(animator->Play("Move"),animator->GetLastError());animator->Pause();
        const int hand=resources.Roles.at(GameSproutRuntime::HumanBone::LeftHand),upper=resources.Roles.at(GameSproutRuntime::HumanBone::LeftUpperArm),lower=resources.Roles.at(GameSproutRuntime::HumanBone::LeftLowerArm);
        Check(Near(Position(first.Nodes[hand]).X,-2)&&Near(Position(first.Nodes[hand]).Y,3.6f),"Retarget neutral did not preserve target reference with different bone axes");
        Check(animator->Seek(.5f),animator->GetLastError());const auto moved=Position(first.Nodes[hand]);Check(moved.Y<3.59f&&!Near(moved.X,-2),"Different-name Human clip was not retargeted");
        Check(Near((Position(first.Nodes[lower])-Position(first.Nodes[upper])).Length(),.8f)&&Near((moved-Position(first.Nodes[lower])).Length(),.6f),"Retarget stretched target limbs");
        auto bad=resources.TargetAvatar;bad.Bones.erase(std::remove_if(bad.Bones.begin(),bad.Bones.end(),[](const auto& value){return value.Bone==GameSproutRuntime::HumanBone::RightFoot;}),bad.Bones.end());std::string error;Check(!GameSproutRuntime::ValidateHumanAvatar(resources.Target,bad,error),"Missing required bone accepted");
        GameSproutRuntime::AnimatorIKGoal goal;goal.Position={-1,3.3f,.3f};goal.PositionWeight=1;goal.Hint={-1,4,1};goal.HintWeight=1;
        const float time=animator->GetPlaybackState().Time;Check(animator->SetIKGoal(GameSproutRuntime::AvatarIKGoal::LeftHand,goal),animator->GetLastError());
        Check((Position(first.Nodes[hand])-goal.Position).Length()<.01f,"Two-bone IK failed reachable hand target");Check(animator->GetPlaybackState().Time==time,"IK preview advanced time");
        GameSproutRuntime::AnimatorLookAt look;look.Position={0,3,3};look.Weight=1;look.BodyWeight=.5f;look.HeadWeight=1;look.ClampWeight=.2f;
        Check(animator->SetLookAt(look)&&((Position(first.Nodes[hand])-goal.Position).Length()<.01f),"LookAt body motion displaced solved hand IK");
        look.Weight=0;Check(animator->SetLookAt(look),"Cannot clear LookAt");
        const float firstBend=Position(first.Nodes[lower]).Z;goal.Hint={-1,4,-2};Check(animator->SetIKGoal(GameSproutRuntime::AvatarIKGoal::LeftHand,goal),animator->GetLastError());
        Check(std::fabs(firstBend-Position(first.Nodes[lower]).Z)>.02f,"Pole hint did not control elbow plane");
        GameSproutRuntime::AnimatorIKGoal foot;foot.Position={-.3f,.25f,.35f};foot.PositionWeight=1;foot.Hint={-.3f,.8f,1};foot.HintWeight=1;
        Check(animator->SetIKGoal(GameSproutRuntime::AvatarIKGoal::LeftFoot,foot),animator->GetLastError());Check((Position(first.Nodes[resources.Roles.at(GameSproutRuntime::HumanBone::LeftFoot)])-foot.Position).Length()<.01f,"Two-bone foot IK failed");
        goal.Position={-100,100,100};Check(animator->SetIKGoal(GameSproutRuntime::AvatarIKGoal::LeftHand,goal),animator->GetLastError());Check(Near((Position(first.Nodes[hand])-Position(first.Nodes[upper])).Length(),1.4f,.01f),"Unreachable IK stretched or produced NaN");
        goal.PositionWeight=0;goal.RotationWeight=1;goal.Rotation=Turn(.5f);Check(animator->SetIKGoal(GameSproutRuntime::AvatarIKGoal::LeftHand,goal),animator->GetLastError());const auto handMatrix=first.Nodes[hand]->GetTransform()->GetWorldMatrix();Check(Near(handMatrix(0,0),std::cos(.5f)),"IK rotation weight not applied independently");
        goal.PositionWeight=std::numeric_limits<float>::quiet_NaN();Check(!animator->SetIKGoal(GameSproutRuntime::AvatarIKGoal::LeftHand,goal),"IK accepted NaN weight");
        animator->ClearIKGoals();Check((Position(first.Nodes[hand])-moved).Length()<.005f,"Clear IK did not restore pure animation preview");
        IKListener listener;listener.Runtime=runtime;listener.Player=&first;runtime->RegisterListener(&listener);listener.Changes=0;
        Check(animator->Play("Move"),animator->GetLastError());runtime->Update(.1f);Check(listener.Calls==1&&listener.Changes==0&&(Position(first.Nodes[hand])-GameSproutRuntime::Vector3{-1,3.3f,.3f}).Length()<.01f,"OnAnimatorIK did not solve same Update without dirtying scene");
        listener.Stop=true;runtime->Update(.1f);Check(!animator->GetPlaybackState().Playing&&animator->GetIKGoal(GameSproutRuntime::AvatarIKGoal::LeftHand).PositionWeight==0,"Stop in IK callback did not cancel safely");
        listener.Stop=false;listener.Destroy=true;Check(animator->Play("Move"),animator->GetLastError());const auto destroyed=first.Object->GetInstanceID();runtime->Update(.1f);Check(!runtime->QueryGameObject(destroyed),"Destroy in IK callback was not safely flushed");runtime->UnregisterListener(&listener);
        auto sync=Make(*scene,resources);GameSproutRuntime::AnimatorLayer layer;layer.Name="Sync";layer.SyncedLayerIndex=0;layer.DefaultWeight=.5f;layer.MotionOverrides={{"Move",{Guid,12}}};resources.Controller.Layers.push_back(layer);++resources.Revision;
        Check(sync.Animator->Play("Move"),sync.Animator->GetLastError());runtime->Update(.4f);Check(Near(sync.Animator->GetPlaybackState(0).NormalizedTime,.4f)&&Near(sync.Animator->GetPlaybackState(1).NormalizedTime,.4f),"Sync did not share source phase");
        Check(sync.Animator->Seek(.25f,1)&&Near(sync.Animator->GetPlaybackState(0).Time,.25f),"Sync Seek did not target owner FSM");
        resources.Controller.Layers[1].SyncedLayerAffectsTiming=true;++resources.Revision;Check(sync.Animator->Play("Move"),sync.Animator->GetLastError());runtime->Update(.3f);Check(Near(sync.Animator->GetPlaybackState().NormalizedTime,.2f),"Sync timing did not lerp source duration with weight");
        Check(sync.Animator->SetLayerWeight(1,1)&&Near(sync.Animator->GetPlaybackState().NormalizedTime,.2f),"Changing sync timing weight jumped phase");runtime->Update(.2f);Check(Near(sync.Animator->GetPlaybackState().NormalizedTime,.3f),"Weight-one sync timing did not replace source duration");
        Check(sync.Animator->CrossFade("Still",.2f,0,1),"Sync CrossFade rejected");runtime->Update(.1f);Check(sync.Animator->GetPlaybackState(0).NextState=="Still"&&sync.Animator->GetPlaybackState(1).NextState=="Still","Sync transition became independent");
        resources.Controller.Parameters.push_back({"Injury",GameSproutRuntime::AnimatorParameterType::Float,0});
        resources.Controller.Layers[1].MotionOverrides={{"Move",{},"Injury",{{{Guid,10},0,1},{{Guid,12},1,1}}}};++resources.Revision;
        Check(sync.Animator->Play("Move"),sync.Animator->GetLastError());runtime->Update(.2f);const float sharedPhase=sync.Animator->GetPlaybackState().NormalizedTime;
        Check(sync.Animator->SetFloat("Injury",1),"Cannot set sync-only blend parameter");
        Check(Near(sync.Animator->GetPlaybackState(0).NormalizedTime,sharedPhase)&&Near(sync.Animator->GetPlaybackState(1).NormalizedTime,sharedPhase),"Sync-only 1D timing parameter jumped source or sync phase");
        resources.Controller.Layers[1].IKPass=true;++resources.Revision;Check(sync.Animator->Play("Move"),sync.Animator->GetLastError());Check(sync.Animator->SetLayerWeight(1,0),"Cannot zero IKPass layer");
        IKListener zero;zero.Player=&sync;runtime->RegisterListener(&zero);runtime->Update(.01f);Check(zero.Calls==2,"Zero-weight IKPass did not receive OnAnimatorIK after base pass");runtime->UnregisterListener(&zero);
        resources.Mask.Mode=GameSproutRuntime::AvatarMaskMode::Humanoid;resources.Mask.DefaultWeight=0;resources.Mask.HumanParts={{GameSproutRuntime::HumanBodyPart::LeftArm,1},{GameSproutRuntime::HumanBodyPart::LeftFingers,0}};
        resources.Controller.Layers[1].Mask={Guid,30};resources.Controller.Layers[1].MotionOverrides={{"Move",{Guid,11}}};resources.Controller.Layers[1].IKPass=false;resources.Controller.Layers[1].SyncedLayerAffectsTiming=false;++resources.Revision;
        Check(sync.Animator->Play("Move"),sync.Animator->GetLastError());sync.Animator->ClearIKGoals();Check(sync.Animator->SetLayerWeight(1,0)&&sync.Animator->Seek(.5f),"Cannot sample body mask baseline");
        const auto baselineLeg=Position(sync.Nodes[resources.Roles.at(GameSproutRuntime::HumanBone::LeftFoot)]),baselineHand=Position(sync.Nodes[hand]);
        const auto fingerBefore=sync.Nodes[resources.Roles.at(GameSproutRuntime::HumanBone::LeftIndexProximal)]->GetTransform()->GetLocalMatrix();
        Check(sync.Animator->SetLayerWeight(1,1),sync.Animator->GetLastError());Check((Position(sync.Nodes[resources.Roles.at(GameSproutRuntime::HumanBone::LeftFoot)])-baselineLeg).Length()<.005f&&(Position(sync.Nodes[hand])-baselineHand).Length()>.02f,"Portable body-part mask did not isolate left arm on target Skeleton");
        const auto fingerAfter=sync.Nodes[resources.Roles.at(GameSproutRuntime::HumanBone::LeftIndexProximal)]->GetTransform()->GetLocalMatrix();
        Check(Near(fingerBefore(0,1),fingerAfter(0,1))&&std::fabs(fingerBefore(0,1))>.05f,"Human finger part was overwritten by hand/arm mask");
        resources.Controller.Layers[1].SyncedLayerIndex=1;++resources.Revision;Check(!sync.Animator->Play("Move"),"Cyclic sync source accepted");
        sync.Animator->Stop();resources.Controller.Layers.resize(1);resources.Controller.Layers[0].IKPass=false;resources.Controller.Layers[0].States[0].Clip={Guid,13};
        resources.Target.Nodes[0].Transform.Rotation={0,std::sin(GameSproutRuntime::Pi/4),0,std::cos(GameSproutRuntime::Pi/4)};resources.TargetAvatar.ReferencePose[0]=resources.Target.Nodes[0].Transform;
        Check(GameSproutRuntime::BuildHumanAvatarAxes(resources.Target,resources.TargetAvatar,error),error);++resources.Revision;
        auto moving=Make(*scene,resources);moving.Object->GetTransform()->SetLocalPosition({4,0,0});IKListener rootListener;rootListener.Player=&moving;runtime->RegisterListener(&rootListener);
        const auto play=[&](GameSproutRuntime::RootMotionMode mode){moving.Animator->Stop();Check(moving.Animator->SetRootMotionMode(mode)&&moving.Animator->Play("Move"),moving.Animator->GetLastError());runtime->Update(.5f);};
        play(GameSproutRuntime::RootMotionMode::Disabled);Check(Near(Position(moving.Object).Z,0)&&Near(Position(moving.Nodes[0]).Z,-1),"Disabled human root did not retain source motion retargeted to target frame");
        play(GameSproutRuntime::RootMotionMode::ExtractOnly);Check(Near(Position(moving.Object).Z,0)&&Near(Position(moving.Nodes[0]).Z,0)&&Near(moving.Animator->GetRootMotionDelta().Translation.Z,-1),"Human extracted root did not rotate frame and scale with stature");
        rootListener.RootCalls=0;play(GameSproutRuntime::RootMotionMode::ApplyToObject);Check(Near(Position(moving.Object).Z,-1)&&Near(Position(moving.Nodes[0]).Z,-1)&&rootListener.RootCalls==1,"Human root applied twice or dispatched more than once");
        rootListener.SolveRoot=true;play(GameSproutRuntime::RootMotionMode::ApplyToObject);Check(Near(Position(moving.Object).Z,-.25f)&&moving.Object->GetTransform()->GetLocalPosition().Z==0,"Solved human motion was not applied once without authoring changes");runtime->UnregisterListener(&rootListener);
        auto generic=Make(*scene,resources);generic.Animator->SetAvatar({});GameSproutRuntime::AnimatorIKGoal unsupported;unsupported.PositionWeight=1;
        Check(!generic.Animator->SetIKGoal(GameSproutRuntime::AvatarIKGoal::LeftHand,unsupported),"Generic Animator accepted active humanoid IK");
        GameSproutRuntime::AnimatorLookAt unsupportedLook;unsupportedLook.Weight=1;Check(!generic.Animator->SetLookAt(unsupportedLook),"Generic Animator accepted active LookAt");
        Check(generic.Animator->SetIKGoal(GameSproutRuntime::AvatarIKGoal::LeftHand,{}),"Generic zero IK goal must remain clearable");generic.Animator->ClearIKGoals();
        runtime->SetResourceProvider(nullptr);delete runtime;std::cout<<"Human retarget, rest axes/proportion preservation, IK/callback safety and synchronized timing passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';delete runtime;return 1;}
}
