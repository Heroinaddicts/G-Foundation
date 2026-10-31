#pragma once
#include "../../Module/Shared/AvatarMaskDefinition.h"
#include "../../Module/RuntimeAssets/RuntimeAssets.h"
#include "AnimationTestSources.h"

namespace {
void PreparedSnapshot(const AssetImportJob& producer,AssetImportRequest& request) {
    request.SourcePaths[producer.Request.GuidString]=producer.Request.SourcePath;
    for(const auto& artifact:producer.Result.Manifest.Assets) {
        AssetImportPreparedResource resource;resource.Type=artifact.Type;resource.DescriptorJson=artifact.DescriptorJson;
        resource.Path=QDir(QString::fromStdString(producer.Stage)).filePath(QString::fromStdString(artifact.Path)).toStdString();
        resource.Size=artifact.DataSize;resource.Version=artifact.DataVersion;resource.Hash=artifact.Hash;
        request.PreparedResources[producer.Request.GuidString+"_"+std::to_string(artifact.LocalID)]=std::move(resource);
    }
}
void CheckMaskSchema() {
    GameSproutRuntime::AvatarMaskData mask;mask.Skeleton={Guid(1,2,3,4).GetGuidString(),7};mask.Nodes={{"/Root/Spine",.75f}};
    auto source=AvatarMaskDefinition::Write(mask,false);GameSproutRuntime::AvatarMaskData parsed;std::string error;
    Check(AvatarMaskDefinition::Read(source,false,parsed,error),error);
    Check(AvatarMaskDefinition::Read(AvatarMaskDefinition::Write(parsed,true),true,parsed,error),error);
    auto defaults=source;defaults.removeMember("DefaultWeight");defaults.removeMember("RootMotionWeight");defaults.removeMember("Nodes");
    Check(AvatarMaskDefinition::Read(defaults,false,parsed,error)&&parsed.DefaultWeight==0&&parsed.RootMotionWeight==0&&parsed.Nodes.empty(),"Mask current defaults failed");
    auto bad=source;bad["Nodes"].append(bad["Nodes"][0]);Check(!AvatarMaskDefinition::Read(bad,false,parsed,error),"Duplicate mask paths accepted");
    bad=source;bad["Nodes"][0]["Weight"]=1.01;Check(!AvatarMaskDefinition::Read(bad,false,parsed,error),"Out of range Mask weight accepted");
    bad=source;bad["RootMotionWeight"]=std::numeric_limits<double>::quiet_NaN();Check(!AvatarMaskDefinition::Read(bad,false,parsed,error),"NaN Mask weight accepted");
    bad=source;bad["Nodes"][0]["Path"]="/Root/../Spine";Check(!AvatarMaskDefinition::Read(bad,false,parsed,error),"Traversal Mask path accepted");
    bad=source;bad["Format"]="AvatarMask0";Check(!AvatarMaskDefinition::Read(bad,true,parsed,error),"Old Mask format accepted");
    GameSproutRuntime::SkeletonData skeleton;GameSproutRuntime::SkeletonNode root;root.Path="/Root";skeleton.Nodes.push_back(root);
    GameSproutRuntime::SkeletonNode spine;spine.Path="/Root/Spine";spine.Parent=0;skeleton.Nodes.push_back(spine);
    Check(AvatarMaskDefinition::Validate(mask,skeleton,error),error);
    mask.Nodes[0].Path="/Unknown";Check(!AvatarMaskDefinition::Validate(mask,skeleton,error),"Unknown Skeleton path accepted");mask.Nodes[0].Path=spine.Path;
    skeleton.Nodes[0].Parent=1;Check(!AvatarMaskDefinition::Validate(mask,skeleton,error),"Cyclic Skeleton accepted");
}
void CheckMaskImport() {
    QTemporaryDir root;Check(root.isValid()&&QDir(root.path()).mkpath("Assets"),"Cannot create Mask fixture");Context context;std::string error;
    TestMeta model(eAssetImporterType::Model,Guid(110,220,330,440),"Body.fbx");Write(root.path()+"/Assets/Body.fbx",Json::Value("prepared skeleton fixture"));
    AssetImportJob skeletonJob(Request(root,model));skeletonJob.Context=&context;skeletonJob.Stage=root.path().toStdString()+"/SkeletonStage";QDir().mkpath(QString::fromStdString(skeletonJob.Stage));
    Json::Value skeleton;skeleton["Nodes"]=Json::Value(Json::arrayValue);
    for(unsigned index=0;index<2;++index) {
        Json::Value node;node["Path"]=index?"/Root/Spine":"/Root";node["Name"]=index?"Spine":"Root";node["Parent"]=index?0:-1;
        auto& pose=node["Transform"];pose["Translation"]=Json::Value(Json::arrayValue);pose["Rotation"]=Json::Value(Json::arrayValue);pose["Scale"]=Json::Value(Json::arrayValue);
        for(unsigned channel=0;channel<3;++channel){pose["Translation"].append(0);pose["Scale"].append(1);}for(unsigned channel=0;channel<4;++channel)pose["Rotation"].append(channel==3?1:0);
        skeleton["Nodes"].append(node);
    }
    Check(skeletonJob.Emit(eAssetType::Skeleton,"skeleton","Skeleton",skeleton),skeletonJob.Result.Error);
    skeletonJob.Result.Manifest.GuidString=model._Guid.GetGuidString();
    model.Identities=AssetArtifactIO::JsonText(skeletonJob.Identities);std::string modelJson;model.SerializeMeta(modelJson);Json::Value metaValue;AssetArtifactIO::Parse(modelJson,metaValue);Write(root.path()+"/Assets/Body.fbx.meta",metaValue);
    const auto skeletonRef=Ref(skeletonJob,eAssetType::Skeleton);
    GameSproutRuntime::AvatarMaskData mask;mask.Skeleton=skeletonRef;mask.Nodes={{"/Root/Spine",.75f}};mask.RootMotionWeight=.25f;
    TestMeta maskMeta(eAssetImporterType::AvatarMask,Guid(111,222,333,444),"Upper.mask");Write(root.path()+"/Assets/Upper.mask",AvatarMaskDefinition::Write(mask,false));
    auto request=Request(root,maskMeta);PreparedSnapshot(skeletonJob,request);AssetImportJob job(request);
    Check(RunAssetImport(job,context),"Mask import: "+job.Result.Error);
    Check(job.Result.Manifest.Assets.size()==1&&job.Result.Manifest.Dependencies.size()==1&&job.Result.Manifest.Dependencies[0].Path=="Body.fbx","Mask Skeleton dependency was not persisted");
    Library library;library.Add(skeletonJob);library.Add(job);LibraryRuntimeResources provider(nullptr,&library);GameSproutRuntime::AvatarMaskData loaded;
    const auto maskRef=Ref(job,eAssetType::AvatarMask);Check(provider.LoadAvatarMask(maskRef,loaded,error),error);
    Check(loaded.Skeleton==skeletonRef&&loaded.Nodes.size()==1&&loaded.Nodes[0].Weight==.75f&&loaded.RootMotionWeight==.25f,"Prepared Mask data changed");
    const auto resource=library.QueryAsset(Guid(maskRef.Guid),maskRef.LocalID);QFile artifact(QString::fromStdString(resource->GetArtifactPath()));Check(artifact.open(QIODevice::ReadOnly),"Cannot read Mask binary");
    Check(!artifact.readAll().trimmed().startsWith('{'),"Mask artifact has a JSON header");artifact.close();
    // Exercise source IO through the real Module API, with value-only test
    // registries replacing the host's database and project discovery.
    TestProject project;project.Root=root.path().toStdString();Sources sourceRegistry;
    MutableMeta registeredMask(eAssetImporterType::AvatarMask,Guid(maskRef.Guid),"Upper.mask",project.Root);
    registeredMask.State=eAssetImportState::Ready;registeredMask.Identities=job.Result.Manifest.SubAssetsJson;
    sourceRegistry.Metas[maskRef.Guid]=&registeredMask;Check(registeredMask.SaveMeta(),"Cannot persist Mask source Meta");
    TestEditor editor;editor.Modules={{"AssetsManager",&sourceRegistry},{"LibraryManager",&library},{"Project",&project}};
    GameSproutEditor::GameSproutModuleApi api(nullptr,&editor);RuntimeAssets module;
    GameSproutEditor::GameSproutEditorFinal::Before(&module,&api);Check(GameSproutEditor::GameSproutEditorFinal::Initialize(&module,&api),"RuntimeAssets source service failed");
    std::string created,snapshot;GameSproutRuntime::AvatarMaskData authoring;
    Check(module.CreateAvatarMaskSource("","FullBody",skeletonRef,created,error)&&created=="FullBody.mask",error);
    Json::Value createdSource;QFile createdFile(root.path()+"/Assets/FullBody.mask");Check(createdFile.open(QIODevice::ReadOnly),"Mask creation used the wrong absolute path");
    Check(AssetArtifactIO::Parse(createdFile.readAll().toStdString(),createdSource)&&createdSource["Skeleton"]["Guid"]==skeletonRef.Guid
        &&createdSource["DefaultWeight"].asFloat()==1&&createdSource["RootMotionWeight"].asFloat()==1,"New Mask defaults or Skeleton identity are incorrect");createdFile.close();
    Check(!module.CreateAvatarMaskSource("","FullBody",skeletonRef,created,error),"Mask creation overwrote an existing source");
    Check(!module.CreateAvatarMaskSource("..","Outside",skeletonRef,created,error),"Mask creation escaped Assets");
    Check(module.LoadAvatarMaskSource(maskRef,authoring,snapshot,error),error);
    auto invalid=authoring;invalid.Nodes[0].Path="/Missing";
    Check(!module.SaveAvatarMaskSource(maskRef,invalid,snapshot,error)&&sourceRegistry.Reimports==0,"Invalid Mask source was saved");
    authoring.DefaultWeight=.4f;authoring.RootMotionWeight=.5f;
    Check(module.SaveAvatarMaskSource(maskRef,authoring,snapshot,error)&&sourceRegistry.Reimports==1,error);
    Check(!module.SaveAvatarMaskSource(maskRef,authoring,snapshot,error)&&sourceRegistry.Reimports==1,"Stale source snapshot overwrote an external edit");
    Check(module.LoadAvatarMaskSource(maskRef,authoring,snapshot,error)&&authoring.DefaultWeight==.4f&&authoring.RootMotionWeight==.5f,error);
    GameSproutEditor::GameSproutEditorFinal::Release(&module,&api);
    Write(root.path()+"/Assets/Upper.mask",AvatarMaskDefinition::Write(mask,false));
    // A stable guid_localid file can be atomically replaced with an equal-length
    // generation after the main thread captured its descriptor. Never decode it
    // using the previous generation's layout.
    const auto skeletonKey=skeletonRef.Guid+"_"+std::to_string(skeletonRef.LocalID);
    const QString skeletonPath=QString::fromStdString(request.PreparedResources.at(skeletonKey).Path);
    QFile skeletonFile(skeletonPath);Check(skeletonFile.open(QIODevice::ReadOnly),"Cannot read Skeleton generation");const auto original=skeletonFile.readAll();skeletonFile.close();
    auto replacement=original;replacement[replacement.size()-1]=static_cast<char>(replacement.back()^1);
    Check(AssetArtifactIO::Write(skeletonPath,replacement,error),error);AssetImportJob replaced(request);
    Check(!RunAssetImport(replaced,context)&&replaced.Result.Superseded&&replaced.Result.Retry,"Equal-length artifact replacement was decoded using stale descriptors");
    Check(AssetArtifactIO::Write(skeletonPath,original,error),error);
    auto bad=request;bad.PreparedResources.clear();AssetImportJob unavailable(bad);Check(!RunAssetImport(unavailable,context),"Unprepared Skeleton silently accepted");
    mask.Nodes[0].Path="/Missing";Write(root.path()+"/Assets/Upper.mask",AvatarMaskDefinition::Write(mask,false));AssetImportJob unknown(request);Check(!RunAssetImport(unknown,context),"Unknown Mask path compiled");
    mask.Nodes[0].Path="/Root/Spine";mask.Skeleton.LocalID=999;Write(root.path()+"/Assets/Upper.mask",AvatarMaskDefinition::Write(mask,false));AssetImportJob identity(request);Check(!RunAssetImport(identity,context),"Wrong Skeleton LocalID accepted");
    auto& record=library.Resources.at(maskRef.Guid+"_"+std::to_string(maskRef.LocalID));Json::Value descriptor;AssetArtifactIO::Parse(record.Artifact.DescriptorJson,descriptor);descriptor["Format"]="AvatarMask0";record.Artifact.DescriptorJson=AssetArtifactIO::JsonText(descriptor);
    Check(!provider.LoadAvatarMask(maskRef,loaded,error),"Provider accepted old Mask descriptor");
}
}
