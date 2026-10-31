#include "../AssetsImport/AssetTestSupport.h"
#include "../../Module/Shared/AnimationDefinition.h"
#include "../../Module/Shared/AvatarMaskDefinition.h"
#include <chrono>

#include "AnimationTestSources.h"
int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);auto* real=Foundation::GetFoundationApi(TEST_BINARY_ROOT);
    try {
        Check(real&&real->Initialize(argc,argv)&&real->Launch(),"Foundation startup failed");
        QTemporaryDir temporary;Check(temporary.isValid()&&QDir(temporary.path()).mkpath("Assets"),"Cannot create fixture");
        TestProject project;project.Root=temporary.path().toStdString();TestIDE ide;TestConsole console;Sources sources;TestEditor editor;
        FoundationProxy foundation;foundation.Real=real;foundation.Tasks.Real=real->TaskApi();foundation.Tasks.IDE=&ide;
        editor.IDE=&ide;editor.Modules={{"Project",&project},{"Console",&console},{"AssetsManager",&sources}};
        for(const std::string name:{"LocalDatabase","LibraryManager","ShaderCompiler","AssetsPipeline","GuidCreator"})editor.Modules[name]=LoadModule(name);
        GameSproutEditor::GameSproutModuleApi api(&foundation,&editor);
        for(const auto& entry:editor.Modules)GameSproutEditor::GameSproutEditorFinal::Before(entry.second,&api);
        for(const auto& entry:editor.Modules)Check(GameSproutEditor::GameSproutEditorFinal::Initialize(entry.second,&api),"Initialize "+entry.first);
        for(const auto& entry:editor.Modules)if(entry.first!="Project")Check(GameSproutEditor::GameSproutEditorFinal::Launch(entry.second,&api),"Launch "+entry.first);
        Check(GameSproutEditor::GameSproutEditorFinal::Launch(&project,&api),"Project launch");
        auto* pipeline=static_cast<IAssetsPipeline*>(editor.Modules["AssetsPipeline"]);
        MutableMeta clip(eAssetImporterType::AnimationClip,Guid(1,2,3,4),"Move.anim",project.Root),controller(eAssetImporterType::AnimatorController,Guid(5,6,7,8),"Move.controller",project.Root);
        MutableMeta mask(eAssetImporterType::AvatarMask,Guid(9,10,11,12),"Body.mask",project.Root),model(eAssetImporterType::Model,Guid(13,14,15,16),"Body.fbx",project.Root);
        for(auto* meta:{&clip,&controller,&mask,&model}){sources.Metas[meta->_Guid.GetGuidString()]=meta;Check(meta->SaveMeta(),"Cannot save Meta");}
        clip.Settings["MainObjectID"]=Json::UInt64(11);Check(clip.SaveMeta(),"Cannot update Clip Meta");
        std::string error;Json::Value clipSource;clipSource["Version"]=1;clipSource["Duration"]=1;clipSource["SampleRate"]=1;clipSource["Tracks"]=Json::Value(Json::arrayValue);clipSource["Tracks"].append("/Root");
        clipSource["Frames"]=Json::Value(Json::arrayValue);
        for(int frame=0;frame<2;++frame){Json::Value value,pose;value["Time"]=frame;pose["Translation"]=Json::Value(Json::arrayValue);pose["Rotation"]=Json::Value(Json::arrayValue);pose["Scale"]=Json::Value(Json::arrayValue);
            for(int channel=0;channel<3;++channel){pose["Translation"].append(0);pose["Scale"].append(1);}for(int channel=0;channel<4;++channel)pose["Rotation"].append(channel==3?1:0);
            value["Poses"]=Json::Value(Json::arrayValue);value["Poses"].append(pose);clipSource["Frames"].append(value);}
        const auto write=[&](const std::string& path,const Json::Value& value){Check(AssetArtifactIO::Write(QString::fromStdString(project.Root+"/Assets/"+path),QByteArray::fromStdString(AssetArtifactIO::JsonText(value)),error),error);};
        write(clip.Path,clipSource);GameSproutRuntime::AnimatorControllerData data;data.Layers.emplace_back();data.Layers[0].Name="Base Layer";
        GameSproutRuntime::AnimatorState state;state.Name="Move";state.Clip={clip._Guid.GetGuidString(),11};data.Layers[0].States.push_back(state);write(controller.Path,AnimationDefinition::ControllerJson(data,false));
        const auto finish=[&](MutableMeta& meta,const AssetImportResult& result){meta.State=result.Success?eAssetImportState::Ready:eAssetImportState::Failed;if(result.Success){meta.Identities=result.Manifest.SubAssetsJson;Check(meta.SaveMeta(),"Persist identities");}};
        const auto pump=[&](const std::function<bool()>& done){const auto begin=std::chrono::steady_clock::now();while(!done()) {real->Update();QCoreApplication::processEvents();QThread::msleep(1);Check(std::chrono::steady_clock::now()-begin<std::chrono::seconds(10),"Dependency queue stalled");}};
        const auto balanced=[&](){Check(ide.Count==0&&ide.Loads==ide.Unloads&&ide.InvalidProgressOperations==0&&ide.AbandonedImportPlans==0,"Loading/progress leaked");for(const auto& item:ide.ImportPlans)Check(item.second.Ended&&item.second.Completed==item.second.Total,"Unfinished original plan");};
        unsigned controllerCalls=0,clipCalls=0;const auto countBefore=foundation.Tasks.Submissions;const auto plansBefore=ide.ImportBegins.size();const auto batch=pipeline->BeginImportBatch(2);
        pipeline->ImportAsset(&controller,[&](const auto& result){++controllerCalls;finish(controller,result);Check(result.Success,result.Error);});
        Check(foundation.Tasks.Submissions==countBefore&&ide.Count>0,"Dependent was submitted before pending Clip completed");
        pipeline->ImportAsset(&clip,[&](const auto& result){++clipCalls;finish(clip,result);Check(result.Success,result.Error);});pipeline->EndImportBatch(batch);
        pump([&](){return controllerCalls==1&&clipCalls==1&&ide.Count==0;});balanced();
        Check(ide.ImportBegins.size()==plansBefore+1&&ide.ImportPlans.at(ide.ImportBegins.back()).Total==2,"Resume grew original denominator");
        std::cout<<"PASS pending Clip completion resumes Controller with a fresh Meta snapshot and fixed denominator\n";
        // Publish a minimal prepared Skeleton through the real Library. This is
        // deliberately an output-only generation change: the FBX source bytes and
        // settings stay unchanged, so source hashing alone cannot invalidate it.
        auto* library=static_cast<ILibraryManager*>(editor.Modules["LibraryManager"]);
        write(model.Path,Json::Value("synthetic prepared Model"));
        Json::Value skeletonBody;skeletonBody["Nodes"]=Json::Value(Json::arrayValue);Json::Value skeletonNode;
        skeletonNode["Path"]="/Root";skeletonNode["Name"]="Root";skeletonNode["Parent"]=-1;skeletonNode["Transform"]=clipSource["Frames"][0]["Poses"][0];skeletonBody["Nodes"].append(skeletonNode);
        Json::Value identities(Json::arrayValue),identity;identity["LocalID"]=Json::UInt64(33);identity["Type"]=static_cast<unsigned>(eAssetType::Skeleton);
        identity["Name"]="Skeleton";identity["SourceKey"]="skeleton";identities.append(identity);model.Identities=AssetArtifactIO::JsonText(identities);model.State=eAssetImportState::Ready;Check(model.SaveMeta(),"Persist Skeleton identity");
        unsigned generation=0;
        const auto publishSkeleton=[&](){
            const std::string stage=library->GetLibraryRoot()+"/Staging/prepared-"+std::to_string(++generation);Check(QDir().mkpath(QString::fromStdString(stage)),"Cannot create prepared stage");
            QByteArray bytes;Check(AssetValueBinary::Encode(skeletonBody,bytes),"Cannot encode Skeleton");Check(AssetArtifactIO::Write(QString::fromStdString(stage+"/33.gsasset"),bytes,error),error);
            AssetArtifact artifact;artifact.LocalID=33;artifact.Type=eAssetType::Skeleton;artifact.Name="Skeleton";artifact.SourceKey="skeleton";artifact.Path="33.gsasset";
            artifact.Hash=AssetArtifactIO::Hash(bytes);artifact.DataSize=static_cast<uint64_t>(bytes.size());Json::Value descriptor;descriptor["Encoding"]="BinaryValueLE1";descriptor["Format"]="StructuredResource";artifact.DescriptorJson=AssetArtifactIO::JsonText(descriptor);
            AssetImportManifest manifest;manifest.GuidString=model._Guid.GetGuidString();manifest.SourcePath=model.Path;manifest.SourceHash=std::string(64,'a');manifest.SettingsHash=std::string(64,'b');manifest.ImporterVersion=6;manifest.SubAssetsJson=model.Identities;manifest.Assets.push_back(artifact);
            Check(library->CommitImport(stage,manifest,error),error);
        };
        publishSkeleton();GameSproutRuntime::AvatarMaskData fullMask;fullMask.Skeleton={model._Guid.GetGuidString(),33};fullMask.DefaultWeight=1;fullMask.RootMotionWeight=1;
        mask.Settings["MainObjectID"]=Json::UInt64(22);Check(mask.SaveMeta(),"Persist Mask settings");write(mask.Path,AvatarMaskDefinition::Write(fullMask,false));
        bool maskDone=false,controllerDone=false;pipeline->ImportAsset(&mask,[&](const auto& result){Check(result.Success,result.Error);finish(mask,result);maskDone=true;});pump([&](){return maskDone&&ide.Count==0;});balanced();
        Check(pipeline->CanReuseAsset(&mask),"Unchanged Mask prepared-input observations did not fast-reuse");
        auto layered=data;auto maskedLayer=layered.Layers[0];maskedLayer.Name="Upper";maskedLayer.Mask={mask._Guid.GetGuidString(),22};layered.Layers.push_back(maskedLayer);
        write(controller.Path,AnimationDefinition::ControllerJson(layered,false));controller.State=eAssetImportState::Pending;
        pipeline->ImportAsset(&controller,[&](const auto& result){Check(result.Success,result.Error);finish(controller,result);controllerDone=true;});pump([&](){return controllerDone&&ide.Count==0;});balanced();
        const auto submissionsBeforeWarm=foundation.Tasks.Submissions;
        Check(pipeline->CanReuseAsset(&mask)&&pipeline->CanReuseAsset(&controller)&&foundation.Tasks.Submissions==submissionsBeforeWarm,"Unchanged animation sources submitted work during fast validation");
        skeletonBody["Nodes"][0]["Name"]="Rout";publishSkeleton();
        Check(!pipeline->CanReuseAsset(&mask)&&!pipeline->CanReuseAsset(&controller),"Changed prepared Skeleton generation was incorrectly fast-reused");
        maskDone=controllerDone=false;mask.State=eAssetImportState::Pending;
        pipeline->ImportAsset(&mask,[&](const auto& result){Check(result.Success&&!result.Cached,"Changed prepared Skeleton skipped Mask semantic compilation");finish(mask,result);maskDone=true;});pump([&](){return maskDone&&ide.Count==0;});
        controller.State=eAssetImportState::Pending;pipeline->ImportAsset(&controller,[&](const auto& result){Check(result.Success&&!result.Cached,"Changed prepared Skeleton skipped Controller semantic compilation");finish(controller,result);controllerDone=true;});pump([&](){return controllerDone&&ide.Count==0;});balanced();
        Check(pipeline->CanReuseAsset(&mask)&&pipeline->CanReuseAsset(&controller),"Recompiled prepared-input baseline was not reusable");
        std::cout<<"PASS unchanged Mask/Controller fast reuse submits zero tasks; output-only Skeleton changes invalidate and recompile both\n";
        AssetImportManifest producer;Check(library->GetManifest(model._Guid,producer)&&producer.Assets.size()==1,"Missing prepared producer");
        const QString preparedPath=QDir(QString::fromStdString(library->GetLibraryRoot()+"/Artifact")).filePath(QString::fromStdString(producer.Assets[0].Path));
        QFile preparedFile(preparedPath);Check(preparedFile.open(QIODevice::ReadOnly),"Cannot read prepared producer");const auto undamaged=preparedFile.readAll();preparedFile.close();
        auto damaged=undamaged;damaged[damaged.size()-1]=static_cast<char>(damaged.back()^1);Check(AssetArtifactIO::Write(preparedPath,damaged,error),error);
        mask.State=eAssetImportState::Pending;unsigned damageCallbacks=0;const auto damageSubmissions=foundation.Tasks.Submissions;
        pipeline->ImportAsset(&mask,[&](const auto& result){++damageCallbacks;Check(!result.Success&&!result.Superseded&&!result.Retry&&result.Error.find("damaged")!=std::string::npos,"Stable producer corruption requested an endless consumer retry");finish(mask,result);});
        pump([&](){return damageCallbacks==1&&ide.Count==0;});balanced();
        Check(foundation.Tasks.Submissions==damageSubmissions+1,"Stable artifact damage repeatedly submitted the consumer");Check(AssetArtifactIO::Write(preparedPath,undamaged,error),error);
        std::cout<<"PASS stable prepared corruption reports one terminal failure, without retrying forever\n";
        clip.State=eAssetImportState::Ready;controller.State=mask.State=model.State=eAssetImportState::Pending;GameSproutRuntime::AvatarMaskData maskData;maskData.Skeleton={model._Guid.GetGuidString(),33};write(mask.Path,AvatarMaskDefinition::Write(maskData,false));write(model.Path,Json::Value("invalid FBX used only for task rejection"));
        auto upper=data.Layers[0];upper.Name="Upper";upper.Mask={mask._Guid.GetGuidString(),22};data.Layers.push_back(upper);write(controller.Path,AnimationDefinition::ControllerJson(data,false));
        foundation.Tasks.Reject=true;unsigned calls=0;const auto rejected=pipeline->BeginImportBatch(3);
        const auto reject=[&](MutableMeta& meta){pipeline->ImportAsset(&meta,[&,target=&meta](const auto& result){++calls;Check(!result.Success,"Rejected dependency succeeded");finish(*target,result);});};
        reject(controller);reject(mask);Check(calls==0,"Pending chain completed too early");reject(model);pipeline->EndImportBatch(rejected);
        Check(calls==3,"Synchronous failure did not wake the whole dependency chain");balanced();foundation.Tasks.Reject=false;
        const auto dependents=pipeline->FindSourceDependents(model.Path);Check(std::find(dependents.begin(),dependents.end(),mask._Guid.GetGuidString())!=dependents.end(),"Failed first import lost its source-declaration dependency edge");
        std::cout<<"PASS Model synchronous rejection wakes Mask and Controller, keeps failed-source dependency edges\n";
        clip.State=controller.State=eAssetImportState::Pending;data.Layers.resize(1);write(controller.Path,AnimationDefinition::ControllerJson(data,false));
        unsigned supersededCalls=0;const auto duplicateBatch=pipeline->BeginImportBatch(2);
        pipeline->ImportAsset(&controller,[&](const auto& result){++supersededCalls;Check(result.Superseded,"Duplicate deferred request lost its cancellation result");});
        pipeline->ImportAsset(&controller,[&](const auto& result){++supersededCalls;Check(result.Superseded,"Invalidated replacement did not cancel");});
        Check(supersededCalls==1,"Duplicate deferred request overwrote the original callback");pipeline->Invalidate(controller._Guid);pipeline->EndImportBatch(duplicateBatch);
        Check(supersededCalls==2,"Replacement callback dispatched more than once");balanced();
        clip.State=controller.State=eAssetImportState::Pending;data.Layers.resize(1);write(controller.Path,AnimationDefinition::ControllerJson(data,false));unsigned cancelled=0;
        pipeline->ImportAsset(&controller,[&](const auto& result){++cancelled;Check(result.Superseded&&!result.Success,"Invalidated deferred result published");finish(controller,result);});
        pipeline->Invalidate(controller._Guid);Check(cancelled==1,"Invalidated deferred callback count is not one");balanced();
        clip.State=controller.State=eAssetImportState::Pending;unsigned dependentFailed=0,dependencyCancelled=0;
        const auto cancelBatch=pipeline->BeginImportBatch(2);
        pipeline->ImportAsset(&controller,[&](const auto& result){++dependentFailed;Check(!result.Success,"Failed dependency used stale compiled data");finish(controller,result);});
        pipeline->ImportAsset(&clip,[&](const auto& result){++dependencyCancelled;Check(result.Superseded&&!result.Success,"Cancelled dependency published");finish(clip,result);});
        pipeline->Invalidate(clip._Guid);pipeline->EndImportBatch(cancelBatch);
        pump([&](){return dependentFailed==1&&dependencyCancelled==1&&ide.Count==0;});balanced();
        std::cout<<"PASS deferred invalidation and asynchronous dependency cancellation dispatch once and balance Loading\n";
        GameSproutEditor::GameSproutEditorFinal::Release(pipeline,&api);GameSproutEditor::GameSproutEditorFinal::Release(editor.Modules["LibraryManager"],&api);real->Release();return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';return 1;}
}
