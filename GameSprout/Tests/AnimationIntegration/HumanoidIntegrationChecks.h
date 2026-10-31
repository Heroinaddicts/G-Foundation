#pragma once
#include "HumanoidIntegrationAssets.h"

namespace {
void CheckHumanoidIntegration(Host& host,LibraryRuntimeResources& resources,Library& library,
    const GameSproutRuntime::ResourceReference& originalModel,const QTemporaryDir& root,Context& context) {
    GameSproutRuntime::ModelData original;GameSproutRuntime::SkeletonData originalSkeleton;std::string error;
    Check(resources.LoadModel(originalModel,original,error)&&resources.LoadSkeleton(original.Skeleton,originalSkeleton,error),error);
    Check(QFile::copy(root.path()+"/Assets/WomanAnimations.fbx",root.path()+"/Assets/HumanoidSource.fbx"),"Cannot copy source human model");
    AssetImportJob sourceJob(HumanModelRequest(root,Guid(131,132,133,134),"HumanoidSource.fbx",WomanHumanProfile(originalSkeleton,false),1));
    Check(RunAssetImport(sourceJob,context),"Humanoid source import failed: "+sourceJob.Result.Error);
    library.Add(sourceJob);PersistLayerMeta(root,sourceJob);
    const auto sourceReference=Ref(sourceJob,eAssetType::Model);GameSproutRuntime::ModelData source;
    GameSproutRuntime::HumanoidAvatarData sourceAvatar;
    Check(resources.LoadModel(sourceReference,source,error)&&!source.Avatar.Guid.empty()
        &&resources.LoadHumanoidAvatar(source.Avatar,sourceAvatar,error),error);
    GameSproutRuntime::SkeletonData sourceSkeleton;Check(resources.LoadSkeleton(source.Skeleton,sourceSkeleton,error),error);
    Check(AssetArtifactIO::Write(root.path()+"/Assets/HumanReference.anim",
        QByteArray::fromStdString(AssetArtifactIO::JsonText(NativeHumanReferenceClip(sourceSkeleton,sourceAvatar))),error),error);
    auto nativeRequest=Request(root,Guid(181,182,183,184),"HumanReference.anim",eAssetImporterType::AnimationClip);
    Json::Value nativeMeta;Check(AssetArtifactIO::Parse(nativeRequest.MetaJson,nativeMeta),"Cannot inspect native human metadata");
    nativeMeta["ImportSettings"]["SourceAvatar"]=AnimationDefinition::Reference(source.Avatar);
    nativeRequest.MetaJson=AssetArtifactIO::JsonText(nativeMeta);nativeRequest.SourcePaths[sourceReference.Guid]="HumanoidSource.fbx";
    CapturePreparedResources(nativeRequest,library);AssetImportJob nativeJob(nativeRequest);
    Check(RunAssetImport(nativeJob,context),"Native Meta.SourceAvatar import failed: "+nativeJob.Result.Error);
    library.Add(nativeJob);PersistLayerMeta(root,nativeJob);
    GameSproutRuntime::AnimationClipData nativeClip;
    Check(resources.LoadAnimationClip(Ref(nativeJob,eAssetType::AnimationClip),nativeClip,error)&&nativeClip.SourceAvatar==source.Avatar,
        "Native animation did not retain its Meta-authored source Avatar");

    WriteHumanPeer(root);const Guid targetGuid(141,142,143,144);
    auto discoveryRequest=Request(root,targetGuid,"HumanoidTarget.fbx",eAssetImporterType::Model);
    Json::Value discoveryMeta;Check(AssetArtifactIO::Parse(discoveryRequest.MetaJson,discoveryMeta),"Cannot inspect target model request");
    discoveryMeta["ImportSettings"]["ScaleFactor"]=1.4f;discoveryRequest.MetaJson=AssetArtifactIO::JsonText(discoveryMeta);
    AssetImportJob discoveryJob(discoveryRequest);Check(RunAssetImport(discoveryJob,context),"Renamed target FBX is invalid: "+discoveryJob.Result.Error);
    // Discovery uses a separate provider: publishing Generic artifacts under the final
    // target identity would hide a real hot-reload/cache bug behind the test fixture.
    Library discoveryLibrary;discoveryLibrary.Add(discoveryJob);LibraryRuntimeResources discovery(nullptr,&discoveryLibrary);
    GameSproutRuntime::ModelData targetDiscovery;GameSproutRuntime::SkeletonData targetSkeleton;
    Check(discovery.LoadModel(Ref(discoveryJob,eAssetType::Model),targetDiscovery,error)
        &&discovery.LoadSkeleton(targetDiscovery.Skeleton,targetSkeleton,error),error);
    auto targetRequest=HumanModelRequest(root,targetGuid,"HumanoidTarget.fbx",WomanHumanProfile(targetSkeleton,true),1.4f);
    Json::Value targetMeta;Check(AssetArtifactIO::Parse(targetRequest.MetaJson,targetMeta)
        &&AssetArtifactIO::Parse(discoveryJob.Result.Manifest.SubAssetsJson,targetMeta["SubAssets"]),"Cannot preserve target local identities");
    targetRequest.MetaJson=AssetArtifactIO::JsonText(targetMeta);AssetImportJob targetJob(targetRequest);
    Check(RunAssetImport(targetJob,context),"Humanoid target import failed: "+targetJob.Result.Error);
    library.Add(targetJob);PersistLayerMeta(root,targetJob);
    const auto targetReference=Ref(targetJob,eAssetType::Model);GameSproutRuntime::ModelData target;
    GameSproutRuntime::HumanoidAvatarData targetAvatar;
    Check(resources.LoadModel(targetReference,target,error)&&resources.LoadHumanoidAvatar(target.Avatar,targetAvatar,error),error);
    Check(std::fabs(targetAvatar.HumanScale/sourceAvatar.HumanScale-1.4f)<.002f,"Imported HumanScale did not follow the target model's metre scale");
    Check(sourceAvatar.Bones.front().Path!=targetAvatar.Bones.front().Path,"Retarget fixture accidentally retained matching humanoid paths");
    std::map<std::string,GameSproutRuntime::ResourceReference> clips;
    for(const auto& reference:source.Animations) {
        clips.emplace(ClipName(sourceJob,reference),reference);GameSproutRuntime::AnimationClipData clip;
        Check(resources.LoadAnimationClip(reference,clip,error)&&clip.SourceAvatar==source.Avatar,"Prepared human clip lost its source Avatar");
    }
    const auto walk=clips.at("WomanWalk"),run=clips.at("WomanRun");
    auto* scene=host.Runtime->CreateScene("Humanoid retarget and IK");
    auto* character=host.Runtime->InstantiateModel(scene,targetReference,nullptr,error);Check(character,error);
    character->SetName("Retarget Character");auto* animator=character->GetComponent<GameSproutRuntime::IAnimatorComponent>();
    Check(animator&&animator->GetAvatar()==target.Avatar,"Model instantiation did not bind its prepared target Avatar");
    animator->SetPlayOnStart(false);animator->SetDefaultClip(walk);Check(animator->Play("",.15f),animator->GetLastError());animator->Pause();
    auto* upper=HumanNode(*host.Runtime,*animator,targetAvatar,GameSproutRuntime::HumanBone::LeftUpperArm);
    auto* elbow=HumanNode(*host.Runtime,*animator,targetAvatar,GameSproutRuntime::HumanBone::LeftLowerArm);
    auto* hand=HumanNode(*host.Runtime,*animator,targetAvatar,GameSproutRuntime::HumanBone::LeftHand);
    const float upperLength=HumanDistance(HumanPosition(*upper),HumanPosition(*elbow));
    const float lowerLength=HumanDistance(HumanPosition(*elbow),HumanPosition(*hand));
    auto* geometry=Geometry(character);GameSproutRuntime::Bounds bounds;
    Check(geometry&&host.Renderer->GetWorldBounds(scene,geometry->GetInstanceID(),bounds),host.Renderer->GetLastError());
    GameSproutRuntime::RenderView view;view.Scene=scene;view.ColorTarget=host.Target;
    view.Camera.Position=bounds.Center+GameSproutRuntime::Vector3{0,.04f,2};
    Check(GameSproutRuntime::TryMakeLookAt(view.Camera.Position,bounds.Center,{0,1,0},view.Camera.View),"Cannot frame retargeted model");
    view.Camera.Projection.Projection=GameSproutRuntime::CameraProjection::Orthographic;
    view.Camera.Projection.OrthographicHeight=std::max(.8f,bounds.Extents.Y*2.8f);
    std::vector<std::uint8_t> first,second;Render(host,view,first);
    GameSproutRuntime::AnimationClipData walkData,runData;
    Check(resources.LoadAnimationClip(walk,walkData,error)&&resources.LoadAnimationClip(run,runData,error),error);
    Check(animator->Seek(walkData.Duration*.65f),animator->GetLastError());Render(host,view,second);
    Check(Different(first,second)>64,"Source human animation did not deform a differently named/scaled target on GPU");
    Check(std::fabs(HumanDistance(HumanPosition(*upper),HumanPosition(*elbow))-upperLength)<.001f
        &&std::fabs(HumanDistance(HumanPosition(*elbow),HumanPosition(*hand))-lowerLength)<.001f,
        "Retargeting copied source limb lengths over the target proportions");

    GameSproutRuntime::AvatarMaskData humanMask;humanMask.Mode=GameSproutRuntime::AvatarMaskMode::Humanoid;
    humanMask.DefaultWeight=0;humanMask.RootMotionWeight=0;
    humanMask.HumanParts={{GameSproutRuntime::HumanBodyPart::LeftArm,1},{GameSproutRuntime::HumanBodyPart::LeftFingers,1}};
    Check(AssetArtifactIO::Write(root.path()+"/Assets/HumanUpper.mask",
        QByteArray::fromStdString(AssetArtifactIO::JsonText(AvatarMaskDefinition::Write(humanMask,false))),error),error);
    AssetImportJob maskJob(Request(root,Guid(161,162,163,164),"HumanUpper.mask",eAssetImporterType::AvatarMask));
    Check(RunAssetImport(maskJob,context),"Portable human mask import failed: "+maskJob.Result.Error);
    library.Add(maskJob);PersistLayerMeta(root,maskJob);const auto mask=Ref(maskJob,eAssetType::AvatarMask);
    GameSproutRuntime::AnimatorControllerData graph;GameSproutRuntime::AnimatorLayer base;base.Name="Locomotion";base.IKPass=true;
    GameSproutRuntime::AnimatorState state;state.Name="Move";state.Clip=walk;base.States={state};
    GameSproutRuntime::AnimatorLayer synced;synced.Name="Injured motion";synced.SyncedLayerIndex=0;
    synced.DefaultWeight=.4f;synced.IKPass=true;synced.Mask=mask;
    GameSproutRuntime::AnimatorMotionOverride overrideMotion;overrideMotion.State="Move";overrideMotion.Clip=run;
    synced.MotionOverrides={overrideMotion};graph.Layers={base,synced};
    Check(AssetArtifactIO::Write(root.path()+"/Assets/Human.controller",
        QByteArray::fromStdString(AssetArtifactIO::JsonText(AnimationDefinition::ControllerJson(graph,false))),error),error);
    auto controllerRequest=Request(root,Guid(151,152,153,154),"Human.controller",eAssetImporterType::AnimatorController);
    controllerRequest.SourcePaths={{sourceReference.Guid,"HumanoidSource.fbx"},{targetReference.Guid,"HumanoidTarget.fbx"},
        {mask.Guid,"HumanUpper.mask"}};
    CapturePreparedResources(controllerRequest,library);AssetImportJob controllerJob(controllerRequest);
    Check(RunAssetImport(controllerJob,context),"Synchronized human Controller compile failed: "+controllerJob.Result.Error);
    library.Add(controllerJob);PersistLayerMeta(root,controllerJob);const auto controller=Ref(controllerJob,eAssetType::AnimatorController);
    auto timingGraph=graph;timingGraph.Layers[1].SyncedLayerAffectsTiming=true;timingGraph.Layers[1].DefaultWeight=1;
    Check(AssetArtifactIO::Write(root.path()+"/Assets/HumanTimed.controller",
        QByteArray::fromStdString(AssetArtifactIO::JsonText(AnimationDefinition::ControllerJson(timingGraph,false))),error),error);
    auto timingRequest=Request(root,Guid(171,172,173,174),"HumanTimed.controller",eAssetImporterType::AnimatorController);
    timingRequest.SourcePaths=controllerRequest.SourcePaths;CapturePreparedResources(timingRequest,library);
    AssetImportJob timingJob(timingRequest);Check(RunAssetImport(timingJob,context),"Timing Controller compile failed: "+timingJob.Result.Error);
    library.Add(timingJob);PersistLayerMeta(root,timingJob);
    animator->SetController(Ref(timingJob,eAssetType::AnimatorController));Check(animator->Play("Move"),animator->GetLastError());
    host.Runtime->Update(.2f);
    const float expectedPhase=.2f/runData.Duration;
    Check(std::fabs(animator->GetPlaybackState(0).NormalizedTime-expectedPhase)<.0002f
        &&std::fabs(animator->GetPlaybackState(1).NormalizedTime-expectedPhase)<.0002f,
        "Compiled synchronization Timing did not replace the shared duration at full weight");
    Check(animator->SetLayerWeight(1,.5f)&&std::fabs(animator->GetPlaybackState().NormalizedTime-expectedPhase)<.0002f,
        "Changing synchronized Timing weight jumped the shared normalized phase");
    Check(animator->Play("Move",.35f,1)&&std::fabs(animator->GetPlaybackState(0).NormalizedTime-.35f)<.0002f,
        "Playing a synchronized layer failed to route to its owning state machine");
    animator->SetController({});
    Check(animator->Play("",.35f),animator->GetLastError());
    auto* leg=HumanNode(*host.Runtime,*animator,targetAvatar,GameSproutRuntime::HumanBone::LeftLowerLeg);
    const auto legBefore=leg->GetTransform()->GetLocalMatrix();
    animator->SetController(controller);Check(animator->Play("Move",.35f),animator->GetLastError());host.Runtime->Update(0);
    Check(LayerMatrixClose(legBefore,leg->GetTransform()->GetLocalMatrix()),"Portable upper-body mask changed the target's unmasked leg");
    const auto sourcePhase=animator->GetPlaybackState(0),syncPhase=animator->GetPlaybackState(1);
    Check(sourcePhase.CurrentState==syncPhase.CurrentState&&std::fabs(sourcePhase.NormalizedTime-syncPhase.NormalizedTime)<.0001f,
        "Synchronized motion acquired an independent state machine or normalized phase");
    HumanIKProbe probe;probe.Runtime=host.Runtime;probe.Object=character->GetInstanceID();
    probe.Goal.Position=HumanPosition(*upper)+(HumanPosition(*hand)-HumanPosition(*upper))*.75f;
    probe.Goal.Position.Z+=upperLength*.15f;probe.Goal.Hint=HumanPosition(*elbow)+GameSproutRuntime::Vector3{0,0,upperLength};
    probe.Goal.PositionWeight=1;probe.Goal.HintWeight=1;probe.Enable=true;
    Check(host.Runtime->RegisterListener(&probe),"Cannot register humanoid IK listener");
    struct HumanListenerScope {
        GameSproutRuntime::IGameSproutRuntime& Runtime;HumanIKProbe& Probe;
        ~HumanListenerScope(){Runtime.UnregisterListener(&Probe);}
    } listenerScope{*host.Runtime,probe};
    Render(host,view,first);const float time=animator->GetPlaybackState().Time;
    GameSproutRuntime::AnimatorLookAt lookAt;
    lookAt.Position=HumanPosition(*HumanNode(*host.Runtime,*animator,targetAvatar,GameSproutRuntime::HumanBone::Head))
        +GameSproutRuntime::Vector3{.1f,.05f,.4f};
    lookAt.Weight=.5f;lookAt.BodyWeight=.25f;lookAt.HeadWeight=.8f;lookAt.ClampWeight=.8f;
    Check(animator->SetLookAt(lookAt),"Humanoid LookAt preview rejected a valid goal");
    host.Runtime->Update(0);Render(host,view,second);
    Check(probe.Passes==std::vector<std::uint32_t>{0,1},"IK callbacks did not run after the full layer pose in layer order");
    Check(HumanDistance(HumanPosition(*hand),probe.Goal.Position)<.002f,"LookAt/body rotation displaced a solved two-bone hand IK goal");
    Check(Different(first,second)>32&&animator->GetPlaybackState().Time==time,"IK failed to affect actual GPU skinning or advanced playback");
    animator->Pause();const auto editedGoal=probe.Goal;auto pausedGoal=editedGoal;pausedGoal.Position.X+=upperLength*.05f;
    Check(animator->SetIKGoal(GameSproutRuntime::AvatarIKGoal::LeftHand,pausedGoal),"Paused IK preview rejected a valid target");
    Check(HumanDistance(HumanPosition(*hand),pausedGoal.Position)<.002f&&probe.Passes.size()==2&&animator->GetPlaybackState().Time==time,
        "Paused goal preview did not update immediately or recursively dispatched IK callbacks");
    Check(probe.AuthoringChanges==0,"IK goals or retargeted poses dirtied the authoring scene");

    SceneDocument::ObjectIDs ids;Json::Value document;
    Check(SceneDocument::Serialize(*scene,ids,document,error),error);
    GameSproutRuntime::IScene* restored=nullptr;Check(SceneDocument::Instantiate(document,*host.Runtime,restored,error),error);
    auto* reopened=restored->FindChild("Retarget Character");Check(reopened,"Scene lost the human character");
    auto* reopenedAnimator=reopened->GetComponent<GameSproutRuntime::IAnimatorComponent>();
    Check(reopenedAnimator->GetAvatar()==target.Avatar&&reopenedAnimator->GetController()==controller
        &&reopenedAnimator->GetIKGoal(GameSproutRuntime::AvatarIKGoal::LeftHand).PositionWeight==0&&reopenedAnimator->GetLookAt().Weight==0,
        "Scene lost the target Avatar or persisted transient IK goals");
    Check(reopenedAnimator->Play("Move",.35f),reopenedAnimator->GetLastError());host.Runtime->Update(0);view.Scene=restored;Render(host,view,second);
    Check(Different(first,second)<256,"Reopened human scene did not reproduce its pre-IK source pose");
    host.Runtime->DestroyScene(restored);host.Runtime->DestroyScene(scene);
    std::cout<<"PASS real FBX Human Avatar -> different-name/1.4x target -> synchronized Controller -> hand IK -> Metal -> Scene source round trip\n";
}
}
