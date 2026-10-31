#include "GameSproutRuntimeExport.h"
#include "RuntimeResourceApi.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

static void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
static bool Near(float a,float b){return std::fabs(a-b)<.002f;}
static const std::string Guid="0123456789abcdef0123456789abcdef";
class Resources final : public GameSproutRuntime::IRuntimeResourceProvider {
public:
    std::uint64_t Revision=1;
    int SkeletonLoads=0,ClipLoads=0,ControllerLoads=0;
    bool InvalidClip=false,InvalidController=false,InvalidModel=false;
    std::uint64_t GetRevision()const override{return Revision;}
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData&,std::string&)override{return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData&,std::string&)override{return false;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MeshRenderData& data,std::string&)override{data={};data.Bones.push_back({"/Root#0/Bone#0",{}});return true;}
    bool LoadSkeleton(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::SkeletonData& data,std::string&)override{
        ++SkeletonLoads;data={};data.Nodes.push_back({"/Root#0","Root",-1,{}});data.Nodes.push_back({"/Root#0/Bone#0","Bone",0,{}});return true;
    }
    bool LoadAnimationClip(const GameSproutRuntime::ResourceReference& ref,GameSproutRuntime::AnimationClipData& data,std::string&)override{
        ++ClipLoads;data={};data.Duration=1;data.SampleRate=1;data.Tracks={"/Root#0","/Root#0/Bone#0"};
        GameSproutRuntime::AnimationFrame a,b;a.Time=0;b.Time=1;a.Poses.resize(2);b.Poses.resize(2);
        a.Poses[1].Position.X=float(ref.LocalID-1)*10;b.Poses[1].Position.X=float(ref.LocalID)*10;
        // Equivalent q/-q samples must interpolate without a spin or zero quaternion.
        b.Poses[1].Rotation={0,0,0,-1};data.Frames={a,b};if(InvalidClip)data.Frames[1].Time=-1;return true;
    }
    bool LoadAnimatorController(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::AnimatorControllerData& data,std::string&)override{
        ++ControllerLoads;data={};data.Layers.resize(1);data.Layers[0].Name="Base Layer";data.Parameters={{"Speed",GameSproutRuntime::AnimatorParameterType::Float,0},{"Go",GameSproutRuntime::AnimatorParameterType::Trigger,0},{"Flag",GameSproutRuntime::AnimatorParameterType::Bool,0},{"Index",GameSproutRuntime::AnimatorParameterType::Int,0}};
        for(int i=0;i<3;++i){GameSproutRuntime::AnimatorState state;state.Name=i==0?"Idle":i==1?"Run":"Jump";state.Clip={Guid,std::uint64_t(i+1)};data.Layers[0].States.push_back(state);}
        GameSproutRuntime::AnimatorTransition run;run.From=0;run.To=1;run.Duration=.2f;run.Conditions.push_back({"Speed",GameSproutRuntime::AnimatorCompare::Greater,.5});data.Layers[0].Transitions.push_back(run);
        GameSproutRuntime::AnimatorTransition jump;jump.From=-1;jump.To=2;jump.Duration=0;jump.Conditions.push_back({"Go",GameSproutRuntime::AnimatorCompare::If,0});data.Layers[0].Transitions.push_back(jump);
        GameSproutRuntime::AnimatorTransition exit;exit.From=2;exit.To=0;exit.HasExitTime=true;exit.ExitTime=.75f;exit.Duration=0;data.Layers[0].Transitions.push_back(exit);
        GameSproutRuntime::AnimatorState blend;blend.Name="Blend";blend.BlendParameter="Speed";blend.Motions={{{Guid,1},0,1},{{Guid,2},1,1}};data.Layers[0].States.push_back(blend);
        if(InvalidController)data.Layers[0].Transitions[0].To=999;return true;
    }
    bool LoadModel(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::ModelData& data,std::string&)override{
        data={};data.Skeleton={Guid,10};data.Animations={{Guid,1}};
        GameSproutRuntime::ModelNode child;child.Path="/Root#0/Bone#0";child.Name="Bone";child.Parent=1;child.Mesh={Guid,20};
        GameSproutRuntime::ModelNode parent;parent.Path="/Root#0";parent.Name="Root";parent.Parent=InvalidModel?0:-1;
        data.Nodes={child,parent};return true;
    }
};
class Events final : public GameSproutRuntime::IRuntimeListener {
public:int Changes=0;void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& e)override{if(e.Type==GameSproutRuntime::SceneNodeEventType::ComponentChanged)++Changes;}
};
struct Character{GameSproutRuntime::IGameObject* Root;GameSproutRuntime::IGameObject* Bone;GameSproutRuntime::IAnimatorComponent* Animator;};
static Character Create(GameSproutRuntime::IScene& scene){
    Character c;c.Root=scene.CreateGameObject("Character");auto* importedRoot=scene.CreateGameObject("Root",c.Root);c.Bone=scene.CreateGameObject("Bone",importedRoot);
    c.Animator=c.Root->AddComponent<GameSproutRuntime::IAnimatorComponent>();Check(c.Animator,"Animator factory is unavailable");
    c.Animator->SetSkeleton({Guid,10});c.Animator->SetController({Guid,11});Check(c.Animator->SetNodeBindings({{"/Root#0",importedRoot->GetInstanceID()},{"/Root#0/Bone#0",c.Bone->GetInstanceID()}}),"Explicit skeleton binding failed");return c;
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    auto* runtime=Export(argv[1]);if(!runtime)return 3;
    Resources resources;runtime->SetResourceProvider(&resources);
    try{
        auto* scene=runtime->CreateScene("Animation");auto first=Create(*scene),second=Create(*scene);
        Check(first.Animator->Play("Idle",.5f),first.Animator->GetLastError());
        Check(second.Animator->Play("Idle",.2f),second.Animator->GetLastError());
        Check(Near(first.Bone->GetTransform()->GetWorldMatrix()(0,3),5),"Pose interpolation is incorrect");
        Check(Near(second.Bone->GetTransform()->GetWorldMatrix()(0,3),2),"Characters share playback state");
        Check(Near(first.Bone->GetTransform()->GetLocalPosition().X,0),"Animation overwrote authoring TRS");
        Check(resources.SkeletonLoads==1&&resources.ControllerLoads==1,"Immutable resources were not shared");
        Events events;runtime->RegisterListener(&events);runtime->Update(0);events.Changes=0;
        first.Animator->Pause();runtime->Update(.1f);
        Check(Near(first.Bone->GetTransform()->GetWorldMatrix()(0,3),5)&&Near(second.Bone->GetTransform()->GetWorldMatrix()(0,3),3),"Pause or instance isolation failed");
        Check(events.Changes==0,"Animation evaluation emitted authoring change events");
        Check(!first.Animator->SetBool("Speed",true),"Parameter type mismatch was accepted");
        Check(first.Animator->SetInt("Index",2)&&first.Animator->SetBool("Flag",true),"Typed parameters failed");
        Check(first.Animator->CrossFade("Run",.2f),first.Animator->GetLastError());runtime->Update(.1f);
        Check(Near(first.Bone->GetTransform()->GetWorldMatrix()(0,3),8.5f),"CrossFade did not sample both advancing clips");
        Check(first.Animator->CrossFade("Jump",.2f),first.Animator->GetLastError());runtime->Update(0);
        Check(Near(first.Bone->GetTransform()->GetWorldMatrix()(0,3),8.5f),"Interrupted crossfade discarded the mixed pose");
        runtime->Update(.2f);Check(first.Animator->GetPlaybackState().CurrentState=="Jump","CrossFade completion failed");
        Check(first.Animator->Play("Idle"),"Cannot restart Idle");Check(first.Animator->SetTrigger("Go"),"Cannot set Trigger");runtime->Update(.01f);
        Check(first.Animator->GetPlaybackState().CurrentState=="Jump","AnyState trigger transition failed");double parameter=1;Check(first.Animator->GetParameter("Go",parameter)&&parameter==0,"Trigger was not consumed");
        runtime->Update(3.3f);Check(first.Animator->GetPlaybackState().CurrentState=="Idle","ExitTime skipped a threshold across several loops");
        Check(first.Animator->SetFloat("Speed",.5f)&&first.Animator->Play("Blend",.5f),"Cannot play 1D BlendTree");
        Check(Near(first.Bone->GetTransform()->GetWorldMatrix()(0,3),10),"1D BlendTree pose blend failed");
        first.Bone->GetTransform()->SetLocalPosition({7,0,0});first.Animator->Stop();Check(Near(first.Bone->GetTransform()->GetWorldMatrix()(0,3),7),"Stop did not restore authoring pose");
        first.Animator->Pause();Check(first.Animator->Play("Idle"),"Cannot play before hot reload");++resources.Revision;runtime->Update(.1f);Check(resources.SkeletonLoads==2,"Hot reload did not invalidate animation resources");
        Check(scene->DestroyGameObject(first.Bone),"Cannot remove a bound bone");runtime->Update(.1f);Check(!first.Animator->GetPlaybackState().Playing&&!first.Animator->GetLastError().empty(),"Deleted bone did not stop safely");
        std::string error;auto* instance=runtime->InstantiateModel(scene,{Guid,40},nullptr,error);Check(instance,error);
        auto* animator=instance->GetComponent<GameSproutRuntime::IAnimatorComponent>();Check(animator&&animator->GetNodeBindings().size()==2,"Model did not create a bound Animator");
        auto* node=instance->FindChild("Root")->FindChild("Bone");auto* geometry=node->GetChild(0);Check(geometry->GetComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>(),"Model did not create a skinned mesh renderer");
        Check(animator->Play("Default",.5f),animator->GetLastError());
        std::vector<GameSproutRuntime::Mat4> matrices;Check(geometry->GetComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>()->GetSkinMatrices({{"/Root#0/Bone#0",{}}},matrices,error),error);
        Check(Near(matrices[0](0,3),0),"Mesh-local bone palette contains object transform twice");
        const auto count=scene->GetChildCount();resources.InvalidModel=true;Check(!runtime->InstantiateModel(scene,{Guid,40},nullptr,error)&&scene->GetChildCount()==count,"Invalid model partially mutated the scene");
        resources.InvalidClip=true;++resources.Revision;Check(!second.Animator->Play("Idle")&&!second.Animator->GetLastError().empty(),"Malformed Clip was accepted");
        runtime->UnregisterListener(&events);runtime->SetResourceProvider(nullptr);delete runtime;runtime=nullptr;
        std::cout<<"Runtime animation, model instantiation and isolation checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';delete runtime;return 1;}
}
