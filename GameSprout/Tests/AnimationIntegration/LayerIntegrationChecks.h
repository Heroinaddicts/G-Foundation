#pragma once
#include "LayerIntegrationAssets.h"

namespace {
void CheckLayerIntegration(Host& host,LibraryRuntimeResources& resources,Library& library,
    const GameSproutRuntime::ResourceReference& modelReference,
    const GameSproutRuntime::ResourceReference& walk,const QTemporaryDir& root,Context& context,
    const GameSproutRuntime::RenderView& camera) {
    GameSproutRuntime::ModelData model;GameSproutRuntime::SkeletonData skeleton;std::string error;
    Check(resources.LoadModel(modelReference,model,error)&&resources.LoadSkeleton(model.Skeleton,skeleton,error),error);
    std::size_t chest=skeleton.Nodes.size(),hip=skeleton.Nodes.size();
    for(std::size_t index=0;index<skeleton.Nodes.size();++index) {
        if(skeleton.Nodes[index].Name=="Chest_M")chest=index;
        if(skeleton.Nodes[index].Name=="Hip_R")hip=index;
    }
    Check(chest<skeleton.Nodes.size()&&hip<skeleton.Nodes.size(),"Woman fixture has no upper/lower body branches");

    const Guid upperGuid(101,102,103,104),maskGuid(111,112,113,114),controllerGuid(121,122,123,124);
    Check(AssetArtifactIO::Write(root.path()+"/Assets/Upper.anim",
        QByteArray::fromStdString(AssetArtifactIO::JsonText(UpperBodySource(skeleton,chest,hip))),error),error);
    auto upperRequest=Request(root,upperGuid,"Upper.anim",eAssetImporterType::AnimationClip);
    Json::Value upperMeta;Check(AssetArtifactIO::Parse(upperRequest.MetaJson,upperMeta),"Cannot parse upper clip metadata");
    AnimationClipImportOptions options;options.Events={{.25f,"UpperMarker","mask-root-disabled"}};
    upperMeta["ImportSettings"]["ClipOptions"]=AnimationImportSettingsJson::Write(options,false);
    upperRequest.MetaJson=AssetArtifactIO::JsonText(upperMeta);AssetImportJob upperJob(upperRequest);
    Check(RunAssetImport(upperJob,context),"Upper .anim production import failed: "+upperJob.Result.Error);
    library.Add(upperJob);PersistLayerMeta(root,upperJob);const auto upper=Ref(upperJob,eAssetType::AnimationClip);

    GameSproutRuntime::AvatarMaskData mask;mask.Skeleton=model.Skeleton;
    mask.Nodes={{skeleton.Nodes[chest].Path,1}}; // Sparse: descendants inherit; hips and root movement remain zero.
    Check(AssetArtifactIO::Write(root.path()+"/Assets/Upper.mask",
        QByteArray::fromStdString(AssetArtifactIO::JsonText(AvatarMaskDefinition::Write(mask,false))),error),error);
    auto maskRequest=Request(root,maskGuid,"Upper.mask",eAssetImporterType::AvatarMask);
    maskRequest.SourcePaths[modelReference.Guid]="WomanAnimations.fbx";CapturePreparedResources(maskRequest,library);
    AssetImportJob maskJob(maskRequest);Check(RunAssetImport(maskJob,context),"Mask production import failed: "+maskJob.Result.Error);
    library.Add(maskJob);PersistLayerMeta(root,maskJob);const auto maskReference=Ref(maskJob,eAssetType::AvatarMask);
    GameSproutRuntime::AvatarMaskData preparedMask;
    Check(resources.LoadAvatarMask(maskReference,preparedMask,error),error);
    Check(preparedMask.Skeleton==model.Skeleton&&preparedMask.Nodes.size()==1&&preparedMask.RootMotionWeight==0,
        "Mask artifact lost its source skeleton, sparse branch or root-motion policy");

    GameSproutRuntime::AnimatorControllerData graph;
    GameSproutRuntime::AnimatorState walkState;walkState.Name="Walk";walkState.Clip=walk;
    GameSproutRuntime::AnimatorState upperState;upperState.Name="Upper";upperState.Clip=upper;
    GameSproutRuntime::AnimatorLayer base;base.Name="Base";base.States={walkState};
    GameSproutRuntime::AnimatorLayer overrideLayer;overrideLayer.Name="Upper Override";
    overrideLayer.Mask=maskReference;overrideLayer.DefaultWeight=.6f;overrideLayer.States={upperState};
    GameSproutRuntime::AnimatorLayer additive;additive.Name="Upper Additive";
    additive.BlendingMode=GameSproutRuntime::AnimatorLayerBlendingMode::Additive;
    additive.Mask=maskReference;additive.DefaultWeight=0;additive.ReferenceClip=upper;additive.ReferenceTime=.5f;additive.States={upperState};
    graph.Layers={base,overrideLayer,additive};
    Check(AssetArtifactIO::Write(root.path()+"/Assets/Layered.controller",
        QByteArray::fromStdString(AssetArtifactIO::JsonText(AnimationDefinition::ControllerJson(graph,false))),error),error);
    auto controllerRequest=Request(root,controllerGuid,"Layered.controller",eAssetImporterType::AnimatorController);
    controllerRequest.SourcePaths={{modelReference.Guid,"WomanAnimations.fbx"},{upperGuid.GetGuidString(),"Upper.anim"},
        {maskGuid.GetGuidString(),"Upper.mask"}};CapturePreparedResources(controllerRequest,library);
    AssetImportJob controllerJob(controllerRequest);
    Check(RunAssetImport(controllerJob,context),"Layered Controller production compilation failed: "+controllerJob.Result.Error);
    library.Add(controllerJob);PersistLayerMeta(root,controllerJob);
    const auto controller=Ref(controllerJob,eAssetType::AnimatorController);

    auto* scene=host.Runtime->CreateScene("Production animation layers");
    auto* character=host.Runtime->InstantiateModel(scene,modelReference,nullptr,error);Check(character,error);
    character->SetName("Layer Character");auto* animator=character->GetComponent<GameSproutRuntime::IAnimatorComponent>();
    animator->SetPlayOnStart(false);animator->SetController(controller);Check(animator->Play("Walk"),animator->GetLastError());
    Check(animator->GetLayerCount()==3&&animator->GetLayerName(1)=="Upper Override"&&std::fabs(animator->GetLayerWeight(1)-.6f)<.0001f,
        "Runtime did not load compiled layers/default weights");
    std::uint64_t chestID=0,hipID=0;
    Check(animator->ResolveBone(skeleton.Nodes[chest].Path,chestID)&&animator->ResolveBone(skeleton.Nodes[hip].Path,hipID),"Layer bones are unbound");
    auto* chestNode=host.Runtime->QueryGameObject(chestID);auto* hipNode=host.Runtime->QueryGameObject(hipID);
    animator->Pause();Check(animator->Seek(.35f,0)&&animator->Seek(.5f,1)&&animator->Seek(.5f,2),animator->GetLastError());
    auto view=camera;view.Scene=scene;std::vector<std::uint8_t> baseline,masked;
    Check(animator->SetLayerWeight(1,0),animator->GetLastError());Render(host,view,baseline);
    const auto baseHip=hipNode->GetTransform()->GetLocalMatrix(),baseChest=chestNode->GetTransform()->GetLocalMatrix();
    LayerProbe probe;probe.Object=character->GetInstanceID();Check(host.Runtime->RegisterListener(&probe),"Cannot watch layers");
    struct LayerListenerScope {
        GameSproutRuntime::IGameSproutRuntime& Runtime;LayerProbe& Probe;
        ~LayerListenerScope(){Runtime.UnregisterListener(&Probe);}
    } listenerScope{*host.Runtime,probe};
    Check(animator->SetLayerWeight(1,1),animator->GetLastError());Render(host,view,masked);
    Check(LayerMatrixClose(baseHip,hipNode->GetTransform()->GetLocalMatrix()),"Upper-body mask overwrote the leg track");
    Check(!LayerMatrixClose(baseChest,chestNode->GetTransform()->GetLocalMatrix()),"Override layer did not affect its selected branch");
    Check(Different(baseline,masked)>64,"Layer/mask evaluation did not reach actual skinned GPU vertices");

    Check(animator->SetLayerWeight(1,0)&&animator->SetLayerWeight(2,1),animator->GetLastError());Render(host,view,masked);
    Check(LayerMatrixClose(baseChest,chestNode->GetTransform()->GetLocalMatrix())&&Different(baseline,masked)<256,
        "Additive sample equal to its reference pose was not zero delta");
    Check(animator->Seek(.9f,2),animator->GetLastError());Render(host,view,masked);
    Check(!LayerMatrixClose(baseChest,chestNode->GetTransform()->GetLocalMatrix())&&LayerMatrixClose(baseHip,hipNode->GetTransform()->GetLocalMatrix())
        &&Different(baseline,masked)>64,"Additive layer failed to preserve lower-body locomotion while modifying upper body");
    Check(animator->SetLayerWeight(2,0),animator->GetLastError());Render(host,view,masked);
    Check(Different(baseline,masked)<256,"A zero-weight layer still altered skinning");
    Check(!animator->SetLayerWeight(0,.5f)&&!animator->SetLayerWeight(3,.5f),"Invalid base or out-of-range layer weight was accepted");

    animator->Stop();Check(animator->Play("Walk",0,0)&&animator->Play("Upper",0,1),animator->GetLastError());
    Check(animator->SetLayerWeight(1,.6f)&&animator->SetLayerWeight(2,0),animator->GetLastError());
    probe.Events.clear();host.Runtime->Update(.3f);
    Check(probe.Events.size()==1&&probe.Events.front().LayerIndex==1&&probe.Events.front().LayerName=="Upper Override"
        &&std::fabs(probe.Events.front().Weight-.6f)<.0001f,
        "Upper animation event was masked as root motion or lost its layer identity/effective weight");
    const auto eventCount=probe.Events.size();const auto time=animator->GetPlaybackState(1).Time;
    Render(host,view,masked);Render(host,view,masked);
    Check(probe.Events.size()==eventCount&&animator->GetPlaybackState(1).Time==time,"Rendering ticked the layer clocks/events");

    // Persist source authoring state, never transient preview weights, clocks or evaluated poses.
    Check(animator->SetLayerWeight(1,.2f)&&animator->SetLayerWeight(2,.4f),animator->GetLastError());
    Check(probe.AuthoringChanges==0,"Layer preview weights or evaluated poses dirtied the authoring scene");
    SceneDocument::ObjectIDs ids;Json::Value document;
    Check(SceneDocument::Serialize(*scene,ids,document,error),error);
    GameSproutRuntime::IScene* restored=nullptr;Check(SceneDocument::Instantiate(document,*host.Runtime,restored,error),error);
    auto* reopened=restored->FindChild("Layer Character");Check(reopened,"Scene lost the layered character");
    auto* restoredAnimator=reopened->GetComponent<GameSproutRuntime::IAnimatorComponent>();
    Check(restoredAnimator->GetController()==controller&&restoredAnimator->Play("Walk"),restoredAnimator->GetLastError());
    Check(std::fabs(restoredAnimator->GetLayerWeight(1)-.6f)<.0001f&&restoredAnimator->GetLayerWeight(2)==0
        &&restoredAnimator->GetPlaybackState(1).Time==0,"Scene restored debug layer weights/clocks instead of Controller defaults");
    host.Runtime->DestroyScene(restored);host.Runtime->DestroyScene(scene);
    std::cout<<"PASS actual Woman skeleton -> .anim/.mask/layered Controller -> Module decoders -> Override/Additive/masked Metal skinning -> Scene reopen\n";
}
}
