#pragma once
#include "../../Module/Shared/HumanoidDefinition.h"

namespace {
void CheckHumanoidSchema() {
    GameSproutRuntime::AnimatorControllerData data;GameSproutRuntime::AnimatorLayer base;base.Name="Base";
    GameSproutRuntime::AnimatorState state;state.Name="Walk";state.Clip={Guid(1,2,3,4).GetGuidString(),5};base.States={state};data.Layers.push_back(base);
    GameSproutRuntime::AnimatorLayer sync;sync.Name="Injured";sync.SyncedLayerIndex=0;sync.SyncedLayerAffectsTiming=true;sync.IKPass=true;
    GameSproutRuntime::AnimatorMotionOverride motion;motion.State="Walk";motion.Clip={Guid(5,6,7,8).GetGuidString(),9};sync.MotionOverrides={motion};data.Layers.push_back(sync);
    auto source=AnimationDefinition::ControllerJson(data,false);GameSproutRuntime::AnimatorControllerData decoded;std::string error;
    Check(source["Layers"][1]["SyncedLayer"]=="Base"&&!source["Layers"][1].isMember("States"),"Sync source duplicated its parent graph or persisted numeric layer indices");
    Check(AnimationDefinition::ReadController(source,false,decoded,error),error);
    Check(decoded.Layers[1].SyncedLayerIndex==0&&decoded.Layers[1].IKPass&&decoded.Layers[1].MotionOverrides[0].State=="Walk","Sync layer contract was lost");
    Check(AnimationDefinition::ReadController(AnimationDefinition::ControllerJson(decoded,true),true,decoded,error),error);
    auto bad=source;bad["Layers"][1]["SyncedLayer"]="Injured";Check(!AnimationDefinition::ReadController(bad,false,decoded,error),"Self-sync was accepted");
    bad=source;bad["Layers"][1]["SyncedLayer"]="Missing";Check(!AnimationDefinition::ReadController(bad,false,decoded,error),"Unknown sync source accepted");
    bad=source;bad["Layers"][1]["States"]=Json::Value(Json::arrayValue);Check(!AnimationDefinition::ReadController(bad,false,decoded,error),"Sync layer silently acquired an independent FSM");
    bad=source;bad["Layers"][1]["MotionOverrides"][0]["State"]="Missing";Check(!AnimationDefinition::ReadController(bad,false,decoded,error),"Unknown override state accepted");
    bad=source;bad["Layers"][1]["BlendingMode"]="Additive";Check(!AnimationDefinition::ReadController(bad,false,decoded,error),"Additive timing modification was accepted");
    auto chain=source;auto extra=source["Layers"][1];extra["Name"]="Chain";extra["SyncedLayer"]="Injured";chain["Layers"].append(extra);
    Check(!AnimationDefinition::ReadController(chain,false,decoded,error),"Sync chain was accepted");
    ModelHumanoidImportSettings settings;GameSproutRuntime::HumanBoneMapping mapping;mapping.Bone=GameSproutRuntime::HumanBone::Hips;mapping.Path="/Root/Hips";settings.HumanBones={mapping};
    HumanoidReferencePose pose;pose.Path=mapping.Path;settings.ReferencePose={pose};ModelHumanoidImportSettings parsed;
    Check(HumanoidDefinition::ReadSettings(HumanoidDefinition::SettingsJson(settings),parsed,error)&&parsed.ReferencePose[0].Path==mapping.Path,error);
    auto invalid=HumanoidDefinition::SettingsJson(settings);invalid["HumanBones"].append(invalid["HumanBones"][0]);
    Check(!HumanoidDefinition::ReadSettings(invalid,parsed,error),"Duplicate human roles and paths accepted");
    invalid=HumanoidDefinition::SettingsJson(settings);invalid["HumanReferencePose"][0]["Path"]="/../Outside";
    Check(!HumanoidDefinition::ReadSettings(invalid,parsed,error),"Unsafe reference pose path accepted");
    GameSproutRuntime::AvatarMaskData humanMask;humanMask.Mode=GameSproutRuntime::AvatarMaskMode::Humanoid;
    humanMask.HumanParts={{GameSproutRuntime::HumanBodyPart::LeftArm,.75f}};GameSproutRuntime::AvatarMaskData parsedMask;
    const auto portable=AvatarMaskDefinition::Write(humanMask,false);
    Check(!portable.isMember("Skeleton")&&AvatarMaskDefinition::Read(portable,false,parsedMask,error)&&AvatarMaskDefinition::Validate(parsedMask,{},error),error);
    Check(AvatarMaskDefinition::Read(AvatarMaskDefinition::Write(parsedMask,true),true,parsedMask,error),error);
    auto badMask=portable;badMask["Skeleton"]=AnimationDefinition::Reference({Guid(1,2,3,4).GetGuidString(),1});
    Check(!AvatarMaskDefinition::Read(badMask,false,parsedMask,error),"Humanoid mask accidentally depended on a source Skeleton");
    badMask=portable;badMask["HumanParts"].append(badMask["HumanParts"][0]);Check(!AvatarMaskDefinition::Read(badMask,false,parsedMask,error),"Duplicate Human body-part weights accepted");
    ModelImportSettings factory;Check(factory.SetHumanoidSettings(settings),"Typed rig setting assignment failed");Json::Value json;
    Check(factory.ConvertToJson(json),"Cannot serialize rig settings");ModelImportSettings restored;Check(restored.ConvertFromJson(json)&&restored.GetHumanoidSettings().ReferencePose[0].Path==mapping.Path,"Factory dropped path-based rig settings");
    AnimationClipImportSettings clipSettings;const GameSproutRuntime::ResourceReference sourceAvatar{Guid(3,4,5,6).GetGuidString(),7};
    Check(clipSettings.SetSourceAvatar(sourceAvatar)&&clipSettings.ConvertToJson(json),"Source Avatar setting failed");AnimationClipImportSettings clipRestored;
    Check(clipRestored.ConvertFromJson(json)&&clipRestored.GetSourceAvatar()==sourceAvatar,"Native clip Meta dropped Source Avatar");
    Check(!clipRestored.SetSourceAvatar({"invalid",7})&&clipRestored.GetSourceAvatar()==sourceAvatar,"Invalid Source Avatar overwrote a valid setting");
    Check(clipRestored.SetSourceAvatar({})&&clipRestored.ConvertToJson(json)&&!json.isMember("SourceAvatar"),"Clearing Source Avatar did not restore Generic semantics");

}
void CheckHumanoidFBX(const QString& file) {
    QTemporaryDir root;Check(root.isValid()&&QDir(root.path()).mkpath("Assets"),"Cannot create Humanoid fixture");
    Check(QFile::copy(file,root.path()+"/Assets/Woman.fbx"),"Cannot copy Humanoid FBX");
    TestMeta meta(eAssetImporterType::Model,Guid(170,171,172,173),"Woman.fbx");Context context;AssetImportJob generic(Request(root,meta));
    Check(RunAssetImport(generic,context),generic.Result.Error);Library library;library.Root=root.path().toStdString()+"/Library";library.Add(generic);
    LibraryRuntimeResources resources(nullptr,&library);GameSproutRuntime::ModelData model;GameSproutRuntime::SkeletonData skeleton;std::string error;
    Check(resources.LoadModel(Ref(generic,eAssetType::Model),model,error)&&resources.LoadSkeleton(model.Skeleton,skeleton,error),error);
    Check(model.Avatar.Guid.empty(),"Generic model was incorrectly tagged Humanoid");RuntimeAssets suggestions;ModelHumanoidImportSettings settings;
    Check(suggestions.SuggestHumanoidMapping(skeleton,settings.HumanBones,error),error);
    std::set<GameSproutRuntime::HumanBone> roles;for(const auto& mapping:settings.HumanBones)roles.insert(mapping.Bone);
    for(unsigned index=0;index<static_cast<unsigned>(GameSproutRuntime::HumanBone::Count);++index)if(GameSproutRuntime::IsRequiredHumanBone(static_cast<GameSproutRuntime::HumanBone>(index)))
        Check(roles.count(static_cast<GameSproutRuntime::HumanBone>(index))!=0,"Woman suggestion missed a required bone");
    auto request=Request(root,meta);Json::Value document;Check(AssetArtifactIO::Parse(request.MetaJson,document),"Cannot parse Model meta");
    document["ImportSettings"]=HumanoidDefinition::SettingsJson(settings);document["ImportSettings"]["AnimationType"]="Humanoid";
    request.MetaJson=AssetArtifactIO::JsonText(document);AssetImportJob humanoid(request);
    Check(RunAssetImport(humanoid,context),"Humanoid production import failed: "+humanoid.Result.Error);library.Add(humanoid);
    Check(resources.LoadModel(Ref(humanoid,eAssetType::Model),model,error),error);
    Check(!model.Avatar.Guid.empty(),"Humanoid Model has no Avatar reference");GameSproutRuntime::HumanoidAvatarData avatar;
    Check(resources.LoadHumanoidAvatar(model.Avatar,avatar,error),error);
    Check(avatar.Skeleton==model.Skeleton&&avatar.ReferencePose.size()==skeleton.Nodes.size()&&avatar.HumanScale>0,"Avatar does not preserve source Skeleton/reference pose/body scale");
    for(const auto& clipReference:model.Animations){GameSproutRuntime::AnimationClipData clip;Check(resources.LoadAnimationClip(clipReference,clip,error)&&clip.SourceAvatar==model.Avatar,error);}
    auto invalidRequest=request;Json::Value invalid=document;invalid["ImportSettings"]["HumanBones"]=Json::Value(Json::arrayValue);invalidRequest.MetaJson=AssetArtifactIO::JsonText(invalid);AssetImportJob missing(invalidRequest);
    Check(!RunAssetImport(missing,context),"Humanoid silently fell back to Generic on missing required mappings");
    invalid=document;HumanoidReferencePose unknown;unknown.Path="/RemovedBone";auto badSettings=settings;badSettings.ReferencePose={unknown};invalid["ImportSettings"]=HumanoidDefinition::SettingsJson(badSettings);invalid["ImportSettings"]["AnimationType"]="Humanoid";
    invalidRequest.MetaJson=AssetArtifactIO::JsonText(invalid);AssetImportJob stale(invalidRequest);Check(!RunAssetImport(stale,context),"Stale path-based reference pose silently targeted a different node");

    TestProject project;project.Root=root.path().toStdString();Sources registry;MutableMeta registered(eAssetImporterType::Model,meta._Guid,"Woman.fbx",project.Root);
    registered.State=eAssetImportState::Ready;registered.Settings=document["ImportSettings"];registered.Identities=humanoid.Result.Manifest.SubAssetsJson;
    registry.Metas[model.Avatar.Guid]=&registered;Check(registered.SaveMeta(),"Cannot save Model metadata");
    Json::Value native;native["Version"]=1;native["Duration"]=1;native["SampleRate"]=1;
    native["Tracks"]=Json::Value(Json::arrayValue);native["Parents"]=Json::Value(Json::arrayValue);native["Frames"]=Json::Value(Json::arrayValue);
    for(const auto& node:skeleton.Nodes){native["Tracks"].append(node.Path);native["Parents"].append(node.Parent);}
    for(unsigned sample=0;sample<2;++sample){Json::Value frame;frame["Time"]=sample;frame["Poses"]=Json::Value(Json::arrayValue);for(const auto& node:skeleton.Nodes)frame["Poses"].append(HumanoidDefinition::Pose(node.Transform));native["Frames"].append(frame);}
    Write(root.path()+"/Assets/Human.anim",native);TestMeta clipMeta(eAssetImporterType::AnimationClip,Guid(190,191,192,193),"Human.anim");auto clipRequest=Request(root,clipMeta);
    Json::Value nativeMeta;Check(AssetArtifactIO::Parse(clipRequest.MetaJson,nativeMeta),"Invalid native clip Meta");nativeMeta["ImportSettings"]["SourceAvatar"]=AnimationDefinition::Reference(model.Avatar);clipRequest.MetaJson=AssetArtifactIO::JsonText(nativeMeta);
    clipRequest.SourcePaths[model.Avatar.Guid]="Woman.fbx";
    for(const auto& entry:library.Resources){AssetImportPreparedResource value;value.Type=entry.second.Artifact.Type;value.Path=entry.second.Path;value.DescriptorJson=entry.second.Artifact.DescriptorJson;value.Hash=entry.second.Artifact.Hash;value.Size=entry.second.Artifact.DataSize;value.Version=entry.second.Artifact.DataVersion;clipRequest.PreparedResources[entry.first]=value;}
    AssetImportJob nativeJob(clipRequest);Check(RunAssetImport(nativeJob,context),"Native Humanoid clip: "+nativeJob.Result.Error);library.Add(nativeJob);
    GameSproutRuntime::AnimationClipData nativeClip;Check(resources.LoadAnimationClip(Ref(nativeJob,eAssetType::AnimationClip),nativeClip,error)&&nativeClip.SourceAvatar==model.Avatar,error);
    auto badNative=native;badNative["Parents"][1]=-1;Write(root.path()+"/Assets/Human.anim",badNative);AssetImportJob wrongParents(clipRequest);
    Check(!RunAssetImport(wrongParents,context),"Native SourceAvatar contradictory Parents were accepted");
    badNative=native;badNative["Tracks"][1]="/NotInAvatar";Write(root.path()+"/Assets/Human.anim",badNative);AssetImportJob wrongTrack(clipRequest);
    Check(!RunAssetImport(wrongTrack,context),"Native SourceAvatar accepted unknown track paths");
    badNative=native;badNative["SourceAvatar"]=AnimationDefinition::Reference(model.Avatar);Write(root.path()+"/Assets/Human.anim",badNative);AssetImportJob obsoleteField(clipRequest);
    Check(!RunAssetImport(obsoleteField,context),"Native source body retained a second SourceAvatar authoring entry point");
    auto missingParents=native;missingParents.removeMember("Parents");Write(root.path()+"/Assets/Human.anim",missingParents);
    auto rootRequest=clipRequest;Json::Value rootMeta;Check(AssetArtifactIO::Parse(rootRequest.MetaJson,rootMeta),"Cannot parse root motion native metadata");
    AnimationClipImportOptions rootOptions;rootOptions.ExtractRootMotion=true;rootOptions.RootMotionNode=skeleton.Nodes.front().Path;
    rootMeta["ImportSettings"]["ClipOptions"]=AnimationImportSettingsJson::Write(rootOptions,false);rootRequest.MetaJson=AssetArtifactIO::JsonText(rootMeta);
    AssetImportJob noHumanParents(rootRequest);Check(!RunAssetImport(noHumanParents,context),"Humanoid root motion accepted inferred incomplete parent ancestry");
    Write(root.path()+"/Assets/Human.anim",native);

    TestEditor editor;editor.Modules={{"AssetsManager",&registry},{"LibraryManager",&library},{"Project",&project}};
    GameSproutEditor::GameSproutModuleApi api(nullptr,&editor);RuntimeAssets module;
    GameSproutEditor::GameSproutEditorFinal::Before(&module,&api);Check(GameSproutEditor::GameSproutEditorFinal::Initialize(&module,&api),"Cannot initialize Rig authoring Module");
    std::string maskPath;Check(module.CreateAvatarMaskSource("","PortableBody",Ref(humanoid,eAssetType::Model),maskPath,error)&&maskPath=="PortableBody.mask",error);
    QFile maskFile(root.path()+"/Assets/PortableBody.mask");Check(maskFile.open(QIODevice::ReadOnly),"Cannot open created portable mask");Json::Value maskSource;Check(AssetArtifactIO::Parse(maskFile.readAll().toStdString(),maskSource)&&maskSource["Mode"]=="Humanoid"&&!maskSource.isMember("Skeleton"),"Humanoid Model creation did not choose a portable body mask");maskFile.close();
    TestMeta maskMeta(eAssetImporterType::AvatarMask,Guid(210,211,212,213),"PortableBody.mask");AssetImportJob maskJob(Request(root,maskMeta));Check(RunAssetImport(maskJob,context),maskJob.Result.Error);library.Add(maskJob);
    const auto maskReference=Ref(maskJob,eAssetType::AvatarMask);GameSproutRuntime::AvatarMaskData portableMask;
    Check(resources.LoadAvatarMask(maskReference,portableMask,error)&&portableMask.Mode==GameSproutRuntime::AvatarMaskMode::Humanoid&&portableMask.Skeleton.Guid.empty(),error);
    MutableMeta registeredMask(eAssetImporterType::AvatarMask,maskMeta._Guid,"PortableBody.mask",project.Root);registeredMask.State=eAssetImportState::Ready;registeredMask.Identities=maskJob.Result.Manifest.SubAssetsJson;registry.Metas[maskReference.Guid]=&registeredMask;
    std::string maskSnapshot;Check(module.LoadAvatarMaskSource(maskReference,portableMask,maskSnapshot,error),error);portableMask.DefaultWeight=0;portableMask.RootMotionWeight=0;portableMask.HumanParts={{GameSproutRuntime::HumanBodyPart::LeftArm,1}};
    Check(module.SaveAvatarMaskSource(maskReference,portableMask,maskSnapshot,error)&&registry.Reimports==1,error);
    Check(!module.SaveAvatarMaskSource(maskReference,portableMask,maskSnapshot,error),"Stale portable mask snapshot was accepted");registry.Reimports=0;
    ModelRigImportSettings rig;GameSproutRuntime::SkeletonData sourceSkeleton;std::string snapshot;
    Check(module.LoadModelRigSource(model.Avatar,rig,sourceSkeleton,snapshot,error)&&rig.AnimationType==eModelAnimationType::Humanoid,error);
    auto badRig=rig;badRig.HumanBones.clear();Check(!module.SaveModelRigSource(model.Avatar,badRig,snapshot,error)&&registry.Reimports==0,"Invalid rig source was persisted");
    rig.HumanScale=avatar.HumanScale*1.05f;Check(module.SaveModelRigSource(model.Avatar,rig,snapshot,error)&&registry.Reimports==1,error);
    Check(!module.SaveModelRigSource(model.Avatar,rig,snapshot,error)&&registry.Reimports==1,"Stale rig snapshot overwrote settings");
    Check(module.LoadModelRigSource(model.Avatar,rig,sourceSkeleton,snapshot,error)&&rig.HumanScale==avatar.HumanScale*1.05f,error);
    std::cout<<"PASS Humanoid Model -> mapped/path-based reference Avatar -> clips/Module provider, Rig source validation and conflict protection\n";
}
}
