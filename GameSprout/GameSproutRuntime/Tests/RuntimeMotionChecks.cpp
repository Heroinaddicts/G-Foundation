#include "GameSproutRuntimeExport.h"
#include "RuntimeResourceApi.h"
#include "Math/TransformMath.h"
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
            const GameSproutRuntime::Vector3 position=ref.LocalID==4?GameSproutRuntime::Vector3{0,0,time*speed}:GameSproutRuntime::Vector3{time*speed,0,0};
            GameSproutRuntime::AnimationFrame frame;frame.Time=time;frame.Poses={{position,rotation,{1,1,1}},{{0,1,0},{},{1,1,1}}};
            if(ref.LocalID==5) {
                auto& bone=frame.Poses[1];bone.Rotation={0,std::sin(time*GameSproutRuntime::Pi/4),0,std::cos(time*GameSproutRuntime::Pi/4)};
                bone.Scale=i==0?GameSproutRuntime::Vector3{-1,2,3}:GameSproutRuntime::Vector3{};
            }
            data.Frames.push_back(frame);data.RootMotion.push_back({position,rotation});}
        data.Events=BoundaryEvents?std::vector<GameSproutRuntime::AnimationEventData>{{0,"Start",""},{1,"End",""}}:std::vector<GameSproutRuntime::AnimationEventData>{{.25f,"Foot","left"},{.75f,"Foot","right"}};if(BadMotion)data.RootMotion.pop_back();return true;
    }
    bool LoadAnimatorController(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::AnimatorControllerData& data,std::string&)override {
        data={};data.Layers.resize(1);data.Layers[0].Name="Base Layer";data.Parameters={{"Speed",GameSproutRuntime::AnimatorParameterType::Float,.5},{"Go",GameSproutRuntime::AnimatorParameterType::Trigger,0}};
        for(int i=0;i<3;++i){GameSproutRuntime::AnimatorState state;state.Name=i==0?"Walk":i==1?"Run":"Turn";state.Clip={Guid,std::uint64_t(i+1)};data.Layers[0].States.push_back(state);}
        GameSproutRuntime::AnimatorState forward;forward.Name="Forward";forward.Clip={Guid,4};data.Layers[0].States.push_back(forward);
        GameSproutRuntime::AnimatorState cameraPose;cameraPose.Name="CameraPose";cameraPose.Clip={Guid,5};cameraPose.Loop=false;data.Layers[0].States.push_back(cameraPose);
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
static void CheckCameraYaw(GameSproutRuntime::ICameraComponent& camera,float radians) {
    const auto world=camera.GetGameObject()->GetTransform()->GetWorldMatrix();
    const GameSproutRuntime::Quaternion rotation{0,std::sin(radians*.5f),0,std::cos(radians*.5f)};
    const auto pose=GameSproutRuntime::RuntimeMath::ComposeTRS({world(0,3),world(1,3),world(2,3)},rotation,{1,1,1});
    GameSproutRuntime::Mat4 expected,actual;
    Check(GameSproutRuntime::RuntimeMath::TryInverseAffine(pose,expected),"Expected camera pose was not invertible");
    for(unsigned column=0;column<4;++column)expected(2,column)=-expected(2,column);
    Check(camera.GetViewMatrix(actual),"Animated negative/zero scale invalidated camera view");
    for(unsigned index=0;index<16;++index)Check(std::fabs(actual.Data[index]-expected.Data[index])<.0005f,
        "Camera reconstructed animation rotation from signed/zero scale instead of its actual quaternion");
}
class PoseObserver final:public GameSproutRuntime::IRuntimePropertyObserver {
public:
    unsigned Calls=0;
    void OnRuntimeObjectPropertiesChanged(std::uint64_t,std::uint64_t)override{++Calls;}
};
static void CheckAnimatedCameraScale(GameSproutRuntime::IGameSproutRuntime& runtime) {
    auto* scene=runtime.CreateScene("Animated camera scale");auto character=Make(*scene);
    auto* bone=character.Root->GetChild(0);auto* cameraObject=scene->CreateGameObject("Camera",bone);
    cameraObject->GetTransform()->SetLocalPosition({.2f,.3f,1});cameraObject->GetTransform()->SetLocalScale({-1,0,3});
    auto* camera=cameraObject->AddComponent<GameSproutRuntime::ICameraComponent>();Check(camera,"Cannot create animated camera");
    Check(character.Animator->PlayClip({Guid,5},0,1,false),character.Animator->GetLastError());
    CheckCameraYaw(*camera,0);
    Check(character.Animator->SeekClip(.5f),character.Animator->GetLastError());CheckCameraYaw(*camera,GameSproutRuntime::Pi*.25f);
    const auto zeroScaleMatrix=bone->GetTransform()->GetLocalMatrix();
    PoseObserver observer;Check(runtime.RegisterObjectPropertyObserver(bone->GetInstanceID(),&observer),"Cannot observe animated camera parent");
    runtime.Update(0);observer.Calls=0;
    Check(character.Animator->SeekClip(1),character.Animator->GetLastError());CheckCameraYaw(*camera,GameSproutRuntime::Pi*.5f);
    const auto rotatedZeroScaleMatrix=bone->GetTransform()->GetLocalMatrix();
    for(unsigned index=0;index<16;++index)Check(zeroScaleMatrix.Data[index]==rotatedZeroScaleMatrix.Data[index],
        "Fixture did not isolate a quaternion change hidden by zero scale");
    runtime.Update(0);Check(observer.Calls>0,"Zero-scale quaternion change failed to notify Runtime property observers");
    runtime.UnregisterObjectPropertyObserver(bone->GetInstanceID(),&observer);character.Animator->StopClip();
    Check(character.Animator->Play("CameraPose"),character.Animator->GetLastError());character.Animator->Pause();
    CheckCameraYaw(*camera,0);
    Check(character.Animator->Seek(.5f),character.Animator->GetLastError());CheckCameraYaw(*camera,GameSproutRuntime::Pi*.25f);
    Check(character.Animator->Seek(1),character.Animator->GetLastError());CheckCameraYaw(*camera,GameSproutRuntime::Pi*.5f);
    character.Animator->Stop();
    auto motion=Make(*scene);auto* motionCamera=motion.Object->AddComponent<GameSproutRuntime::ICameraComponent>();
    const float authoredYaw=GameSproutRuntime::Pi/6;
    Check(motion.Object->GetTransform()->SetLocalRotation({0,std::sin(authoredYaw*.5f),0,std::cos(authoredYaw*.5f)}),"Cannot author root-motion camera yaw");
    for(const auto scale:{GameSproutRuntime::Vector3{-1,2,3},GameSproutRuntime::Vector3{}}) {
        motion.Object->GetTransform()->SetLocalScale(scale);
        Check(motion.Animator->SetRootMotionMode(GameSproutRuntime::RootMotionMode::ApplyToObject)&&motion.Animator->Play("Turn"),motion.Animator->GetLastError());
        runtime.Update(.5f);CheckCameraYaw(*motionCamera,authoredYaw+GameSproutRuntime::Pi*.25f);
        motion.Animator->Stop();CheckCameraYaw(*motionCamera,authoredYaw);
    }
    Check(runtime.DestroyScene(scene),"Cannot clean animated camera fixture");
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
        runtime->UnregisterListener(&listener);
        Check(runtime->DestroyScene(scene),"Cannot clean animation fixture");
        auto* forwardScene=runtime->CreateScene("Unity forward root motion");
        auto straight=Make(*forwardScene),turnedForward=Make(*forwardScene);
        straight.Object->GetTransform()->SetLocalPosition({0,0,0});
        const float halfTurn=std::sqrt(.5f);
        Check(turnedForward.Object->GetTransform()->SetLocalRotation({0,halfTurn,0,halfTurn}),"Cannot rotate root-motion object +90 Y");
        for(auto* animator:{straight.Animator,turnedForward.Animator})
            Check(animator->SetRootMotionMode(GameSproutRuntime::RootMotionMode::ApplyToObject)&&animator->Play("Forward"),animator->GetLastError());
        runtime->Update(.5f);
        const auto straightWorld=straight.Object->GetTransform()->GetWorldMatrix();
        const auto turnedWorld=turnedForward.Object->GetTransform()->GetWorldMatrix();
        Check(Near(straightWorld(2,3),.5f)&&Near(straightWorld(0,3),0)&&
              Near(turnedWorld(0,3),10.5f)&&Near(turnedWorld(2,3),0),
              "Local +Z root motion did not become world +Z or +X after a +90 Y object turn");
        Check(Near(straight.Root->GetTransform()->GetLocalMatrix()(2,3),0)&&
              Near(straight.Object->GetTransform()->GetLocalPosition().Z,0),
              "Forward root motion was applied twice or changed authored values");
        straight.Animator->Stop();turnedForward.Animator->Stop();
        Check(runtime->DestroyScene(forwardScene),"Cannot clean forward root-motion fixture");
        CheckAnimatedCameraScale(*runtime);
        // Physics getters expose the latest simulated pose. Accumulated root
        // motion must still compose against authored TRS, or Kinematic movement
        // will be added a second time after every physics write-back.
        auto* motionScene=runtime->CreateScene("Kinematic root motion");
        auto kinematic=Make(*motionScene);
        auto* body=kinematic.Object->AddComponent<GameSproutRuntime::IRigidbodyComponent>();
        auto settings=body->GetSettings();settings.MotionType=GameSproutRuntime::PhysicsMotionType::Kinematic;
        Check(body->SetSettings(settings),"Cannot configure Kinematic root-motion body");
        Check(kinematic.Animator->SetRootMotionMode(GameSproutRuntime::RootMotionMode::ApplyToObject),"Cannot enable Kinematic root motion");
        std::string error;Check(runtime->BeginExecution(error),error);
        Check(kinematic.Animator->Play("Walk"),kinematic.Animator->GetLastError());runtime->Update(0);
        for(int i=0;i<10;++i){runtime->Update(.1f);runtime->FixedUpdate(.1f);}
        Check(Near(kinematic.Object->GetTransform()->GetWorldMatrix()(0,3),11.0f),"Kinematic root motion accumulated the physics result twice");
        Check(Near(kinematic.Object->GetTransform()->GetLocalPosition().X,11.0f),"Gameplay Transform getter did not expose simulated position");
        Check(runtime->EndExecution(error),error);
        kinematic.Animator->Stop();
        Check(Near(kinematic.Object->GetTransform()->GetLocalPosition().X,10.0f),"Kinematic root motion changed the authored baseline");
        runtime->SetResourceProvider(nullptr);delete runtime;
        std::cout<<"Root motion, marker/state notifications and callback lifetime checks passed\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';delete runtime;return 1;}
}
