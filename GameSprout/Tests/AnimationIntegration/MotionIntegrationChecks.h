#pragma once
#include "../../Module/SceneManager/SceneDocument.h"
#include "../../Module/Shared/AnimationImportSettingsJson.h"
#include <thread>

namespace {
Json::Value MotionValues(std::initializer_list<float> values) {
    Json::Value result(Json::arrayValue);for(float value:values)result.append(value);return result;
}
class MotionProbe final : public GameSproutRuntime::IRuntimeListener {
public:
    const std::thread::id Thread=std::this_thread::get_id();
    std::uint64_t Object=0;
    bool Consume=false,Solve=false,Submitted=false,DuplicateRejected=false;
    GameSproutRuntime::IGameSproutRuntime* Runtime=nullptr;
    std::vector<GameSproutRuntime::AnimationNotification> Events;
    std::vector<GameSproutRuntime::RootMotionNotification> Motion;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent&) override {}
    void OnAnimationEvent(const GameSproutRuntime::AnimationNotification& event) override {
        Check(std::this_thread::get_id()==Thread,"Animation callback escaped the owning thread");
        if(event.ObjectID==Object)Events.push_back(event);
    }
    bool OnRootMotion(const GameSproutRuntime::RootMotionNotification& event) override {
        Check(std::this_thread::get_id()==Thread,"Root motion callback escaped the owning thread");
        if(event.ObjectID!=Object)return false;
        Motion.push_back(event);
        if(Solve) {
            auto* object=Runtime?Runtime->QueryGameObject(Object):nullptr;
            auto* animator=object?object->GetComponent<GameSproutRuntime::IAnimatorComponent>():nullptr;
            auto solved=event.Delta;solved.Translation=solved.Translation*0.25f;
            Submitted=animator&&animator->ApplyRootMotionDelta(solved);
            DuplicateRejected=animator&&!animator->ApplyRootMotionDelta(solved);
        }
        return Consume;
    }
    std::size_t Steps()const {
        return std::count_if(Events.begin(),Events.end(),[](const auto& event){return event.Type==GameSproutRuntime::AnimationNotificationType::ClipEvent;});
    }
};
void CheckMotionIntegration(Host& host,LibraryRuntimeResources& resources,Library& library,
    const GameSproutRuntime::ResourceReference& modelReference,const QTemporaryDir& root,Context& context,
    const GameSproutRuntime::RenderView& camera) {
    GameSproutRuntime::ModelData model;GameSproutRuntime::SkeletonData skeleton;std::string error;
    Check(resources.LoadModel(modelReference,model,error)&&resources.LoadSkeleton(model.Skeleton,skeleton,error),error);
    Json::Value source;source["Version"]=1;source["Duration"]=1;source["SampleRate"]=1;
    source["Tracks"]=Json::Value(Json::arrayValue);source["Parents"]=Json::Value(Json::arrayValue);
    source["Frames"]=Json::Value(Json::arrayValue);
    std::size_t selected=skeleton.Nodes.size();
    for(std::size_t index=0;index<skeleton.Nodes.size();++index) {
        source["Tracks"].append(skeleton.Nodes[index].Path);source["Parents"].append(skeleton.Nodes[index].Parent);
        if(skeleton.Nodes[index].Parent<0){Check(selected==skeleton.Nodes.size(),"Fixture requires one imported top-level source node");selected=index;}
    }
    Check(selected<skeleton.Nodes.size(),"Imported model has no top-level source node");
    for(unsigned sample=0;sample<2;++sample) {
        Json::Value frame;frame["Time"]=sample;frame["Poses"]=Json::Value(Json::arrayValue);
        for(std::size_t index=0;index<skeleton.Nodes.size();++index) {
            const auto& pose=skeleton.Nodes[index].Transform;
            Json::Value value;value["Translation"]=MotionValues({pose.Position.X+(index==selected?sample*2.0f:0.0f),pose.Position.Y,pose.Position.Z});
            auto rotation=pose.Rotation;
            if(sample&&index==selected) {
                const GameSproutRuntime::Quaternion turn{0,std::sqrt(0.5f),0,std::sqrt(0.5f)};
                rotation={turn.W*rotation.X+turn.Y*rotation.Z,turn.W*rotation.Y+turn.Y*rotation.W,
                    turn.W*rotation.Z-turn.Y*rotation.X,turn.W*rotation.W-turn.Y*rotation.Y};
            }
            value["Rotation"]=MotionValues({rotation.X,rotation.Y,rotation.Z,rotation.W});
            value["Scale"]=MotionValues({pose.Scale.X,pose.Scale.Y,pose.Scale.Z});frame["Poses"].append(std::move(value));
        }
        source["Frames"].append(std::move(frame));
    }
    Check(AssetArtifactIO::Write(root.path()+"/Assets/Trajectory.anim",QByteArray::fromStdString(AssetArtifactIO::JsonText(source)),error),error);
    auto request=Request(root,Guid(91,92,93,94),"Trajectory.anim",eAssetImporterType::AnimationClip);
    Json::Value meta;Check(AssetArtifactIO::Parse(request.MetaJson,meta),"Cannot inspect native animation Meta");
    AnimationClipImportOptions options;options.ExtractRootMotion=true;options.RootMotionNode=skeleton.Nodes[selected].Path;
    options.Events={{0.25f,"Footstep","left"},{0.75f,"Footstep","right"}};
    meta["ImportSettings"]["ClipOptions"]=AnimationImportSettingsJson::Write(options,false);
    request.MetaJson=AssetArtifactIO::JsonText(meta);AssetImportJob imported(request);
    Check(RunAssetImport(imported,context),"Production motion import failed: "+imported.Result.Error);library.Add(imported);
    const auto reference=Ref(imported,eAssetType::AnimationClip);GameSproutRuntime::AnimationClipData prepared;
    Check(resources.LoadAnimationClip(reference,prepared,error),error);
    Check(prepared.RootMotion.size()==2&&prepared.Events.size()==2&&std::fabs(prepared.RootMotion[1].Translation.X-2)<0.0001f,
        "Production artifact did not retain trajectory/events");
    Check(prepared.Frames.back().Poses[selected].Position.X==skeleton.Nodes[selected].Transform.Position.X+2,
        "Offline root extraction destroyed the original source pose");

    auto* scene=host.Runtime->CreateScene("Production root motion");
    auto* character=host.Runtime->InstantiateModel(scene,modelReference,nullptr,error);Check(character,error);
    character->SetName("Motion Character");character->GetTransform()->SetLocalPosition({10,0,0});
    auto* animator=character->GetComponent<GameSproutRuntime::IAnimatorComponent>();
    animator->SetDefaultClip(reference);animator->SetPlayOnStart(false);
    std::uint64_t rootID=0;Check(animator->ResolveBone(options.RootMotionNode,rootID),"Root path did not bind the imported instance");
    auto* motionNode=host.Runtime->QueryGameObject(rootID);Check(motionNode,"Root node is not live");
    const float restX=motionNode->GetTransform()->GetWorldMatrix().Data[12];
    MotionProbe probe;probe.Object=character->GetInstanceID();probe.Runtime=host.Runtime;Check(host.Runtime->RegisterListener(&probe),"Cannot subscribe to animation");
    struct ListenerScope {
        GameSproutRuntime::IGameSproutRuntime& Runtime;MotionProbe& Probe;
        ~ListenerScope(){Runtime.UnregisterListener(&Probe);}
    } listenerScope{*host.Runtime,probe};
    const auto close=[](float left,float right){return std::fabs(left-right)<0.0002f;};
    const auto play=[&](GameSproutRuntime::RootMotionMode mode) {
        animator->Stop();Check(animator->SetRootMotionMode(mode),"Cannot configure root mode");
        probe.Events.clear();probe.Motion.clear();Check(animator->Play(),animator->GetLastError());host.Runtime->Update(.5f);
        Check(probe.Steps()==1,"Expected one Footstep crossing in 0..0.5 seconds");
        for(const auto& event:probe.Events)if(event.Type==GameSproutRuntime::AnimationNotificationType::ClipEvent)
            Check(event.Clip==reference&&event.Name=="Footstep"&&event.Payload=="left"&&event.SceneID==scene->GetInstanceID(),
                "Animation callback lost its GUID/LocalID/source identity");
    };
    play(GameSproutRuntime::RootMotionMode::Disabled);
    Check(close(character->GetTransform()->GetWorldMatrix().Data[12],10)&&close(motionNode->GetTransform()->GetWorldMatrix().Data[12],restX+1),
        "Disabled root mode changed the character or stripped source movement");
    play(GameSproutRuntime::RootMotionMode::ExtractOnly);
    Check(close(character->GetTransform()->GetWorldMatrix().Data[12],10)&&close(motionNode->GetTransform()->GetWorldMatrix().Data[12],restX)
        &&close(animator->GetRootMotionDelta().Translation.X,1),"ExtractOnly did not expose delta while retaining a stationary character");
    play(GameSproutRuntime::RootMotionMode::ApplyToObject);
    Check(close(character->GetTransform()->GetWorldMatrix().Data[12],11)&&close(motionNode->GetTransform()->GetWorldMatrix().Data[12],restX+1),
        "Applied trajectory was omitted or counted twice");
    Check(character->GetTransform()->GetLocalPosition().X==10,"Applied motion changed source authoring transforms");
    const auto characterMatrix=character->GetTransform()->GetWorldMatrix();
    Check(close(characterMatrix(0,0),std::sqrt(0.5f))&&close(characterMatrix(0,2),std::sqrt(0.5f))
        &&character->GetTransform()->GetLocalRotation().W==1,"Full root rotation was doubled or changed authoring rotation");
    auto view=camera;view.Scene=scene;view.Camera.Position.X+=11;view.Camera.View(0,3)-=11;
    std::vector<std::uint8_t> before,after;Render(host,view,before);
    const auto count=probe.Events.size(),moves=probe.Motion.size();const auto time=animator->GetPlaybackState().Time;
    auto game=view;game.Camera.Projection.OrthographicHeight*=1.1f;Render(host,game,after);
    Check(probe.Events.size()==count&&probe.Motion.size()==moves&&animator->GetPlaybackState().Time==time,
        "Scene/Game Render advanced the animation or emitted notifications");
    animator->Pause();host.Runtime->Update(.2f);
    Check(probe.Events.size()==count&&probe.Motion.size()==moves,"Paused animation emitted motion/events");
    Check(animator->Seek(.9f),animator->GetLastError());host.Runtime->Update(0);
    Check(probe.Events.size()==count&&probe.Motion.size()==moves,"Seek emitted traversed events or root deltas");

    // Persist through the production SceneManager source-document implementation.
    SceneDocument::ObjectIDs ids;Json::Value document;
    Check(SceneDocument::Serialize(*scene,ids,document,error),error);
    Check(AssetArtifactIO::Write(root.path()+"/Assets/Motion.scene",QByteArray::fromStdString(AssetArtifactIO::JsonText(document)),error),error);
    QFile file(root.path()+"/Assets/Motion.scene");Check(file.open(QIODevice::ReadOnly),"Cannot reopen persisted scene file");
    Json::Value reloaded;Check(AssetArtifactIO::Parse(file.readAll().toStdString(),reloaded),"Cannot parse persisted scene");
    GameSproutRuntime::IScene* restored=nullptr;
    Check(SceneDocument::Instantiate(reloaded,*host.Runtime,restored,error),error);
    auto* restoredCharacter=restored->FindChild("Motion Character");Check(restoredCharacter,"Scene lost the character");
    auto* restoredAnimator=restoredCharacter->GetComponent<GameSproutRuntime::IAnimatorComponent>();
    Check(restoredAnimator->GetRootMotionMode()==GameSproutRuntime::RootMotionMode::ApplyToObject&&restoredAnimator->GetDefaultClip()==reference,
        "Scene did not preserve root mode or GUID/LocalID");
    Check(close(restoredCharacter->GetTransform()->GetWorldMatrix().Data[12],10)&&restoredAnimator->GetPlaybackState().Time==0,
        "Scene restored transient movement or playback time");
    Check(restoredAnimator->Play(),restoredAnimator->GetLastError());host.Runtime->Update(.5f);
    Check(close(restoredCharacter->GetTransform()->GetWorldMatrix().Data[12],11),"Reopened scene failed to play the production root trajectory");
    view.Scene=restored;Render(host,view,after);Check(Different(before,after)<256,"Reopened scene did not reproduce the same GPU root-motion pose");
    probe.Consume=true;play(GameSproutRuntime::RootMotionMode::ApplyToObject);
    Check(close(character->GetTransform()->GetWorldMatrix().Data[12],10)&&!probe.Motion.empty(),"Motion callback consumption did not suppress automatic application");
    Check(!animator->ApplyRootMotionDelta({}),"Solved root motion was accepted outside its notification");
    probe.Consume=false;probe.Solve=true;play(GameSproutRuntime::RootMotionMode::ApplyToObject);
    Check(probe.Submitted&&probe.DuplicateRejected&&close(character->GetTransform()->GetWorldMatrix().Data[12],10.25f)
        &&character->GetTransform()->GetLocalPosition().X==10,"Collision-solved root delta was not applied exactly once without changing authoring data");
    animator->Stop();Check(close(character->GetTransform()->GetWorldMatrix().Data[12],10),"Stop did not restore source position");
    host.Runtime->DestroyScene(restored);host.Runtime->DestroyScene(scene);
    std::cout<<"PASS production .anim -> Module provider -> root modes/events -> persisted .scene -> Metal; no double motion or render-driven ticks\n";
}
}
