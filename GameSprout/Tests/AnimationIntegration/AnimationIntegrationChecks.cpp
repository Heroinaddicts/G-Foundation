#include "AssetImportJob.h"
#include "ILibraryManager.h"
#include "../../Module/RuntimeAssets/LibraryRuntimeResources.h"
#include "../../Module/Shared/AnimationDefinition.h"
#include "GameSproutRuntimeExport.h"
#include "RuntimeRenderApi.h"
#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QFile>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
void Check(bool condition,const std::string& error) {if(!condition)throw std::runtime_error(error);}
class Context final : public Foundation::ITaskContext {
public:
    UInt64 GetTaskId()const override{return 1;}
    bool IsCancellationRequested()const override{return false;}
    void* GetContext()const override{return nullptr;}
    bool SetOwnedContext(void*,Foundation::fTaskOwnedContextRelease)override{return false;}
};
class Resource final : public IAsset {
public:
    Resource(const Guid& guid,const AssetArtifact& artifact,std::string path):Identity(guid),Artifact(artifact),Path(std::move(path)){}
    Guid Identity;AssetArtifact Artifact;std::string Path;
    const Guid& GetGuid()const override{return Identity;}
    uint64_t GetLocalID()const override{return Artifact.LocalID;}
    eAssetType GetAssetType()const override{return Artifact.Type;}
    const std::string& GetName()const override{return Artifact.Name;}
    const std::string& GetArtifactPath()const override{return Path;}
    const std::string& GetDataDescriptorJson()const override{return Artifact.DescriptorJson;}
    uint32_t GetDataVersion()const override{return Artifact.DataVersion;}
    uint64_t GetDataSize()const override{return Artifact.DataSize;}
};
// Only the database lookup is replaced. All files, descriptors and production decoders are real.
class Library final : public ILibraryManager {
public:
    std::map<std::string,Resource> Resources;std::string Root;
    void Add(const AssetImportJob& job) {
        for(const auto& artifact:job.Result.Manifest.Assets) {
            Resource resource(Guid(job.Result.Manifest.GuidString),artifact,
                QDir(QString::fromStdString(job.Stage)).filePath(QString::fromStdString(artifact.Path)).toStdString());
            Resources.emplace(job.Result.Manifest.GuidString+"_"+std::to_string(artifact.LocalID),std::move(resource));
        }
    }
    IAsset* QueryAsset(const Guid& guid,uint64_t id)override {
        const auto found=Resources.find(guid.GetGuidString()+"_"+std::to_string(id));return found==Resources.end()?nullptr:&found->second;
    }
    ILDBSession* GetLDBSession()const override{return nullptr;}
    const std::string& GetLibraryRoot()const override{return Root;}
    std::vector<std::string> GetManifestGuids()const override{return {};}
    bool GetManifest(const Guid&,AssetImportManifest&)const override{return false;}
    bool CommitImport(const std::string&,AssetImportManifest&,std::string&)override{return false;}
    bool RemoveAsset(const Guid&,std::string&,const std::vector<LDBWriteOperation>&)override{return false;}
    std::vector<std::string> FindDependents(const std::string&)const override{return {};}
protected:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
struct Host {
    GameSproutRuntime::IGameSproutRuntime* Runtime=nullptr;
    GameSproutRuntime::IRuntimeViewport* Renderer=nullptr;
    ~Host(){delete Renderer;delete Runtime;}
};
AssetImportRequest Request(const QTemporaryDir& root,const Guid& guid,const std::string& path,eAssetImporterType importer) {
    AssetImportRequest request;request.AssetsRoot=root.path().toStdString()+"/Assets";
    request.LibraryRoot=root.path().toStdString()+"/Library";request.SourcePath=path;request.GuidString=guid.GetGuidString();request.Importer=importer;
    Json::Value meta;meta["Version"]=1;meta["Guid"]=guid.GetGuidString();meta["ImporterType"]=importer==eAssetImporterType::Model?"Model":importer==eAssetImporterType::AnimationClip?"AnimationClip":importer==eAssetImporterType::AvatarMask?"AvatarMask":"AnimatorController";
    meta["Path"]=path;meta["ImportSettings"]=Json::Value(Json::objectValue);meta["SubAssets"]=Json::Value(Json::arrayValue);
    request.MetaJson=AssetArtifactIO::JsonText(meta);return request;
}
GameSproutRuntime::ResourceReference Ref(const AssetImportJob& job,eAssetType type) {
    for(const auto& asset:job.Result.Manifest.Assets)if(asset.Type==type)return {job.Result.Manifest.GuidString,asset.LocalID};
    throw std::runtime_error("Expected resource was not imported");
}
std::string ClipName(const AssetImportJob& job,const GameSproutRuntime::ResourceReference& ref) {
    for(const auto& asset:job.Result.Manifest.Assets)if(asset.LocalID==ref.LocalID)return asset.Name;
    throw std::runtime_error("Animation has no resource identity");
}
GameSproutRuntime::IGameObject* Geometry(GameSproutRuntime::IGameObject* root) {
    std::vector<GameSproutRuntime::IGameObject*> pending{root};
    while(!pending.empty()) {auto* object=pending.back();pending.pop_back();
        if(object->GetComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>())return object;
        for(std::size_t index=0;index<object->GetChildCount();++index)pending.push_back(object->GetChild(index));}
    return nullptr;
}
void Render(Host& host,const GameSproutRuntime::RenderView& view,std::vector<std::uint8_t>& pixels) {
    Check(host.Renderer->Render(view),host.Renderer->GetLastError());
    Check(host.Renderer->GetStatistics().Triangles>0 && host.Renderer->GetStatistics().SkippedObjects==0,
        "Actual FBX geometry was skipped: "+host.Renderer->GetLastError());
    std::string error;Check(host.Renderer->ReadPixels(pixels,error),error);
}
std::size_t Different(const std::vector<std::uint8_t>& left,const std::vector<std::uint8_t>& right) {
    Check(left.size()==right.size(),"GPU readback extents differ");std::size_t count=0;
    for(std::size_t index=0;index<left.size();++index)if(left[index]!=right[index])++count;return count;
}
}

#include "MotionIntegrationChecks.h"
#include "LayerIntegrationChecks.h"
#include "HumanoidIntegrationChecks.h"

int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    try {
        Check(argc==2||argc==3,"Pass WomanAnimations.fbx and an optional capture PNG filename");
        QTemporaryDir root;Check(root.isValid()&&QDir(root.path()).mkpath("Assets"),"Cannot create isolated import fixture");
        if(qEnvironmentVariableIsSet("GAMESPROUT_KEEP_ANIMATION_FIXTURE")){root.setAutoRemove(false);std::cout<<"Animation fixture: "<<root.path().toStdString()<<"\n";}
        Check(QFile::copy(QString::fromUtf8(argv[1]),root.path()+"/Assets/WomanAnimations.fbx"),"Cannot copy the source FBX");
        const Guid modelGuid(71,72,73,74);AssetImportJob modelJob(Request(root,modelGuid,"WomanAnimations.fbx",eAssetImporterType::Model));
        Context context;Check(RunAssetImport(modelJob,context),"Real FBX import failed: "+modelJob.Result.Error);
        Library library;library.Root=root.path().toStdString()+"/Library";library.Add(modelJob);
        LibraryRuntimeResources resources(nullptr,&library);std::string error;
        const auto modelReference=Ref(modelJob,eAssetType::Model);GameSproutRuntime::ModelData model;
        Check(resources.LoadModel(modelReference,model,error),error);Check(model.Nodes.size()==35&&model.Animations.size()==4,"Unexpected Woman model hierarchy/clips");
        std::map<std::string,GameSproutRuntime::ResourceReference> clips;
        for(const auto& ref:model.Animations)clips.emplace(ClipName(modelJob,ref),ref);
        for(const std::string name:{"WomanWalk","WomanRun","WomanIdle","WomanBreathing"})Check(clips.count(name)!=0,"Missing Woman clip: "+name);

        // Compile a real Controller through the production Pipeline, using the FBX's persisted subresource IDs.
        Json::Value meta;Check(AssetArtifactIO::Parse(modelJob.Request.MetaJson,meta),"Invalid generated source metadata");
        Check(AssetArtifactIO::Parse(modelJob.Result.Manifest.SubAssetsJson,meta["SubAssets"]),"Invalid imported subresource identities");
        Check(AssetArtifactIO::Write(root.path()+"/Assets/WomanAnimations.fbx.meta",QByteArray::fromStdString(AssetArtifactIO::JsonText(meta)),error),error);
        GameSproutRuntime::AnimatorControllerData graph;graph.Layers.emplace_back();graph.Layers[0].Name="Base Layer";graph.Parameters.push_back({"Speed",GameSproutRuntime::AnimatorParameterType::Float,0});
        for(const std::string name:{"WomanIdle","WomanWalk","WomanRun"}) {GameSproutRuntime::AnimatorState state;state.Name=name;state.Clip=clips.at(name);graph.Layers[0].States.push_back(state);}
        const auto transition=[&](int from,unsigned to,GameSproutRuntime::AnimatorCompare compare,double threshold) {
            GameSproutRuntime::AnimatorTransition value;value.From=from;value.To=to;value.Duration=.1f;
            value.Conditions.push_back({"Speed",compare,threshold});graph.Layers[0].Transitions.push_back(value);
        };
        transition(0,1,GameSproutRuntime::AnimatorCompare::Greater,.1);transition(1,2,GameSproutRuntime::AnimatorCompare::Greater,1.5);
        transition(1,0,GameSproutRuntime::AnimatorCompare::Less,.1);transition(2,1,GameSproutRuntime::AnimatorCompare::Less,1.5);
        Check(AssetArtifactIO::Write(root.path()+"/Assets/Woman.controller",
            QByteArray::fromStdString(AssetArtifactIO::JsonText(AnimationDefinition::ControllerJson(graph,false))),error),error);
        auto controllerRequest=Request(root,Guid(81,82,83,84),"Woman.controller",eAssetImporterType::AnimatorController);
        controllerRequest.SourcePaths[modelGuid.GetGuidString()]="WomanAnimations.fbx";AssetImportJob controllerJob(controllerRequest);
        Check(RunAssetImport(controllerJob,context),"Actual Controller compilation failed: "+controllerJob.Result.Error);library.Add(controllerJob);

        Host host;host.Runtime=Export(TEST_RUNTIME_ROOT);Check(host.Runtime,"Cannot load the independently built Runtime SDK");
        host.Runtime->SetResourceProvider(&resources);
        host.Renderer=host.Runtime->CreateOffscreenRenderer(1280,720,&resources,error);
        if(!host.Renderer){std::cout<<"SKIP Runtime GPU unavailable after successful FBX import: "<<error<<'\n';return 77;}
        auto* scene=host.Runtime->CreateScene("Woman actual Runtime");
        auto* first=host.Runtime->InstantiateModel(scene,modelReference,nullptr,error);Check(first,error);
        auto* animator=first->GetComponent<GameSproutRuntime::IAnimatorComponent>();Check(animator,"Model did not create Animator");
        Check(animator->GetNodeBindings().size()==35,"Model did not bind the complete node hierarchy");
        auto* geometry=Geometry(first);Check(geometry,"Model did not create SkinnedMeshRenderer");
        GameSproutRuntime::Bounds bounds;Check(host.Renderer->GetWorldBounds(scene,geometry->GetInstanceID(),bounds),host.Renderer->GetLastError());
        GameSproutRuntime::RenderView view;view.Scene=scene;view.DrawGrid=false;view.ClearColor={.035f,.045f,.06f,1};
        view.Camera.Position=bounds.Center+GameSproutRuntime::Vector3{0,.08f,2};
        Check(GameSproutRuntime::TryMakeLookAt(view.Camera.Position,bounds.Center,{0,1,0},view.Camera.View),"Cannot make explicit Scene camera");
        view.Camera.Projection.Projection=GameSproutRuntime::CameraProjection::Orthographic;
        view.Camera.Projection.OrthographicHeight=std::max(.8f,bounds.Extents.Y*2.7f);
        std::vector<std::uint8_t> pixels,previous;
        for(const auto& clip:clips) {
            GameSproutRuntime::AnimationClipData data;Check(resources.LoadAnimationClip(clip.second,data,error),error);
            animator->SetDefaultClip(clip.second);Check(animator->Play("",.15f),animator->GetLastError());animator->Pause();
            Render(host,view,previous);Check(animator->Seek(data.Duration*.65f),animator->GetLastError());Render(host,view,pixels);
            const auto difference=Different(previous,pixels);Check(difference>64,"Clip did not deform actual GPU vertices: "+clip.first);
            std::cout<<clip.first<<": "<<difference<<" changed GPU bytes\n";
        }
        auto* second=host.Runtime->InstantiateModel(scene,modelReference,nullptr,error);Check(second,error);
        auto* secondAnimator=second->GetComponent<GameSproutRuntime::IAnimatorComponent>();Check(secondAnimator,"Second Animator missing");
        first->GetTransform()->SetLocalPosition({-.45f,0,0});second->GetTransform()->SetLocalPosition({.45f,0,0});
        animator->SetDefaultClip(clips.at("WomanWalk"));secondAnimator->SetDefaultClip(clips.at("WomanWalk"));
        Check(animator->Play("",0)&&secondAnimator->Play("",0),"Cannot start independent instances");
        Check(animator->SetSpeed(1)&&secondAnimator->SetSpeed(2),"Cannot set independent animation speeds");
        host.Runtime->Update(.125f);
        Check(std::fabs(animator->GetPlaybackState().Time-.125f)<.0001f&&std::fabs(secondAnimator->GetPlaybackState().Time-.25f)<.0001f,
            "Shared resources also shared their runtime playback state");
        const auto firstTime=animator->GetPlaybackState().Time,secondTime=secondAnimator->GetPlaybackState().Time;
        Render(host,view,pixels);auto game=view;game.Camera.Position={1,.5f,2};
        Check(GameSproutRuntime::TryMakeLookAt(game.Camera.Position,bounds.Center,{0,1,0},game.Camera.View),"Cannot make Game camera");Render(host,game,pixels);
        Check(animator->GetPlaybackState().Time==firstTime&&secondAnimator->GetPlaybackState().Time==secondTime,"Scene/Game rendering advanced animation twice");
        animator->Pause();secondAnimator->Pause();Check(animator->Seek(.7f),animator->GetLastError());
        Check(secondAnimator->GetPlaybackState().Time==secondTime,"Seeking one model altered the other instance");

        const auto controller=Ref(controllerJob,eAssetType::AnimatorController);
        animator->SetController(controller);secondAnimator->SetController(controller);
        Check(animator->Play("WomanIdle")&&secondAnimator->Play("WomanIdle"),"Cannot play compiled Controller");
        Check(animator->SetFloat("Speed",0)&&secondAnimator->SetFloat("Speed",2),"Cannot set Controller parameters");
        for(int frame=0;frame<8;++frame)host.Runtime->Update(.05f);
        Check(animator->GetPlaybackState().CurrentState=="WomanIdle"&&secondAnimator->GetPlaybackState().CurrentState=="WomanRun",
            "Real compiled state machine did not independently transition Idle/Walk/Run");Render(host,game,pixels);

        CheckMotionIntegration(host,resources,library,modelReference,root,context,view);
        CheckLayerIntegration(host,resources,library,modelReference,clips.at("WomanWalk"),root,context,view);
        CheckHumanoidIntegration(host,resources,library,modelReference,root,context);

        const std::string names[]={"WomanWalk","WomanRun","WomanIdle","WomanBreathing"};
        GameSproutRuntime::IGameObject* models[4]={first,second,nullptr,nullptr};
        for(int index=0;index<4;++index) {
            if(!models[index])models[index]=host.Runtime->InstantiateModel(scene,modelReference,nullptr,error);
            Check(models[index],error);models[index]->SetName(names[index]);models[index]->GetTransform()->SetLocalPosition({(index-1.5f)*.55f,0,0});
            auto* player=models[index]->GetComponent<GameSproutRuntime::IAnimatorComponent>();player->SetController({});player->SetDefaultClip(clips.at(names[index]));
            Check(player->SetSpeed(1)&&player->Play("",.35f),player->GetLastError());player->Pause();
        }
        view.DrawGrid=true;view.Camera.Projection.OrthographicHeight=1.25f;view.Camera.Position={0,.72f,2.5f};
        Check(GameSproutRuntime::TryMakeLookAt(view.Camera.Position,{0,.27f,0},{0,1,0},view.Camera.View),"Cannot frame final Runtime capture");
        Render(host,view,pixels);
        if(argc==3&&argv[2][0]) {
            const QImage image(pixels.data(),1280,720,1280*4,QImage::Format_RGBA8888);Check(image.save(QString::fromUtf8(argv[2])),"Cannot save real Runtime GPU capture");
            std::cout<<"Actual Runtime capture: "<<argv[2]<<" (left to right: Walk / Run / Idle / Breathing)\n";
        }
        std::cout<<"PASS real FBX -> Pipeline -> Module provider -> Runtime -> GPU; four clips, independent instances, compiled FSM and two render views\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
