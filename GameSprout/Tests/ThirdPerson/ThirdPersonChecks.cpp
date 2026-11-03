#include "ScriptCompiler.h"
#include "AssetImportJob.h"
#include "LibraryRuntimeResources.h"
#include "ILibraryManager.h"
#include "SceneDocument.h"
#include "PrefabDocument.h"
#include "GameSproutRuntimeExport.h"
#include "RuntimeRenderApi.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QLibrary>
#include <QImage>
#include <iostream>
#include <cmath>
#include <stdexcept>

namespace {
void Check(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
QByteArray Read(const QString& path){QFile file(path);Check(file.open(QIODevice::ReadOnly),path.toStdString());return file.readAll();}
Json::Value Parse(const QByteArray& bytes){Json::Value value;Check(AssetArtifactIO::Parse(bytes.toStdString(),value),"JSON parse");return value;}
class Context final:public Foundation::ITaskContext {
public:UInt64 GetTaskId()const override{return 1;}bool IsCancellationRequested()const override{return false;}void* GetContext()const override{return nullptr;}bool SetOwnedContext(void*,Foundation::fTaskOwnedContextRelease)override{return false;}
};
class Asset final:public IAsset {
public:
    Asset(const Guid& guid,const AssetArtifact& artifact,std::string path):GuidValue(guid),Artifact(artifact),Path(std::move(path)){}
    Guid GuidValue;AssetArtifact Artifact;std::string Path;
    const Guid& GetGuid()const override{return GuidValue;}uint64_t GetLocalID()const override{return Artifact.LocalID;}eAssetType GetAssetType()const override{return Artifact.Type;}
    const std::string& GetName()const override{return Artifact.Name;}const std::string& GetArtifactPath()const override{return Path;}const std::string& GetDataDescriptorJson()const override{return Artifact.DescriptorJson;}
    uint32_t GetDataVersion()const override{return Artifact.DataVersion;}uint64_t GetDataSize()const override{return Artifact.DataSize;}
};
// Only DB lookup is replaced; production importers and prepared-resource decoders are used.
class Library final:public ILibraryManager {
public:
    std::map<std::string,Asset> Values;std::string Root;
    void Add(AssetImportJob& job){for(const auto& artifact:job.Result.Manifest.Assets){Asset asset(Guid(job.Request.GuidString),artifact,QDir(QString::fromStdString(job.Stage)).filePath(QString::fromStdString(artifact.Path)).toStdString());Values.emplace(job.Request.GuidString+"_"+std::to_string(artifact.LocalID),asset);}}
    IAsset* QueryAsset(const Guid& guid,uint64_t id)override{auto found=Values.find(guid.GetGuidString()+"_"+std::to_string(id));return found==Values.end()?nullptr:&found->second;}
    ILDBSession* GetLDBSession()const override{return nullptr;}const std::string& GetLibraryRoot()const override{return Root;}std::vector<std::string> GetManifestGuids()const override{return {};}
    bool GetManifest(const Guid&,AssetImportManifest&)const override{return false;}bool CommitImport(const std::string&,AssetImportManifest&,std::string&)override{return false;}bool RemoveAsset(const Guid&,std::string&,const std::vector<LDBWriteOperation>&)override{return false;}std::vector<std::string> FindDependents(const std::string&)const override{return {};}
protected:bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
GameSproutRuntime::IGameObject* Named(GameSproutRuntime::IGameObject* root,const std::string& name){if(root->GetName()==name)return root;for(size_t i=0;i<root->GetChildCount();++i)if(auto* result=Named(root->GetChild(i),name))return result;return nullptr;}
}
int main(int argc,char** argv){
    QGuiApplication app(argc,argv);QTemporaryDir temporary;QLibrary logic;std::vector<AssetImportJob*> jobs;GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try{
        const QString project=temporary.path()+"/Project";QProcess generate;generate.start(TEST_PYTHON,{TEST_GENERATOR,"--output",project});Check(generate.waitForFinished(30000)&&generate.exitCode()==0,generate.readAllStandardError().toStdString());
        // An existing target is a hard failure, including an empty directory.
        generate.start(TEST_PYTHON,{TEST_GENERATOR,"--output",project});Check(generate.waitForFinished(30000)&&generate.exitCode()!=0,"Generator overwrote existing project");
        ScriptCompiler compiler;ScriptCompileRequest request;request.AssetsRoot=(project+"/Assets").toStdString();request.BuildRoot=(project+"/Library/ScriptBuild").toStdString();
        for(const QString name:{"ThirdPersonCharacter","ThirdPersonCamera"}){const std::string path=("Scripts/"+name+".hpp").toStdString();auto meta=Parse(Read(project+"/Assets/"+QString::fromStdString(path)+".meta"));request.Sources.push_back({path,meta["Guid"].asString()});}
        ScriptCompileResult compiled;Check(compiler.Compile(request,compiled,[]{return false;}),compiled.Diagnostics);
        Library library;library.Root=(project+"/Library").toStdString();Context context;
        GameSproutRuntime::ResourceReference characterMaterial;
        for(const QString path:{"CharacterRig.fbx","Animations/Idle.anim","Animations/Walk.anim","Animations/Run.anim","Animations/Jump.anim","Character.physicmaterial"}){
            auto meta=Parse(Read(project+"/Assets/"+path+".meta"));AssetImportRequest import;import.AssetsRoot=request.AssetsRoot;import.LibraryRoot=library.Root;import.SourcePath=path.toStdString();import.GuidString=meta["Guid"].asString();import.Importer=path.endsWith("fbx")?eAssetImporterType::Model:path.endsWith("physicmaterial")?eAssetImporterType::PhysicsMaterial:eAssetImporterType::AnimationClip;import.MetaJson=AssetArtifactIO::JsonText(meta);
            auto* job=new AssetImportJob(import);jobs.push_back(job);Check(RunAssetImport(*job,context),"Import "+path.toStdString()+": "+job->Result.Error);library.Add(*job);
            if(import.Importer==eAssetImporterType::PhysicsMaterial) {
                Check(job->Result.Manifest.Assets.size()==1&&job->Result.Manifest.Assets[0].Type==eAssetType::PhysicsMaterial,"Character material did not produce a physics artifact");
                characterMaterial={import.GuidString,job->Result.Manifest.Assets[0].LocalID};
            }
        }
        logic.setFileName(QString::fromStdString(compiled.LibraryPath));Check(logic.load(),logic.errorString().toStdString());auto entry=reinterpret_cast<GameSproutRuntime::fGetScriptModule>(logic.resolve(GameSproutRuntime::ScriptModuleEntryName));Check(entry&&entry()->Count==2,"Native sample scripts unavailable");
        runtime=Export(TEST_RUNTIME);Check(runtime,"Runtime load");std::string error;
        for(size_t i=0;i<entry()->Count;++i)Check(runtime->RegisterScriptType(entry()->Types[i],error),error);
        LibraryRuntimeResources resources(nullptr,&library);runtime->SetResourceProvider(&resources);
        const auto source=Read(project+"/Assets/Playground.scene");const auto document=Parse(source);GameSproutRuntime::IScene* scene=nullptr;SceneDocument::ObjectIDs ids;
        Check(SceneDocument::Instantiate(document,*runtime,scene,ids,error),error);
        auto* player=Named(scene,"Player");auto* camera=Named(scene,"Main Camera");auto* rig=Named(scene,"Rig");Check(player&&camera&&rig,"Scene objects missing");
        auto* body=player->GetComponent<GameSproutRuntime::IRigidbodyComponent>();auto* animator=rig->GetComponent<GameSproutRuntime::IAnimatorComponent>();
        Json::Value baseline;Check(SceneDocument::Serialize(*scene,ids,baseline,error),error);Check(runtime->BeginExecution(error),error);
        const float dt=1.f/60;
        const auto tick=[&](int count){for(int i=0;i<count;++i){runtime->FixedUpdate(dt);runtime->Update(dt);runtime->CompleteInputFrame();}};
        const auto key=[&](GameSproutRuntime::InputKey key,bool down){GameSproutRuntime::RuntimeInputEvent event;event.Type=down?GameSproutRuntime::InputEventType::KeyDown:GameSproutRuntime::InputEventType::KeyUp;event.Key=key;Check(runtime->SubmitInputEvent(event),"Input key");};
        GameSproutRuntime::RuntimeInputEvent focus;focus.Type=GameSproutRuntime::InputEventType::FocusGained;Check(runtime->SubmitInputEvent(focus),"Focus input");
        tick(90);Check(std::fabs(player->GetTransform()->GetLocalPosition().Y-1)<.15f,"Player failed to settle on ground");Check(animator->GetLastError().empty(),animator->GetLastError());
        if(argc==2) {
            auto* viewport=runtime->CreateOffscreenRenderer(960,540,&resources,error);Check(viewport,error);
            try {
                GameSproutRuntime::RenderView view;view.Scene=scene;view.DrawGrid=false;
                auto* cameraComponent=camera->GetComponent<GameSproutRuntime::ICameraComponent>();Check(cameraComponent->GetViewMatrix(view.Camera.View),"Camera view");
                const auto matrix=camera->GetTransform()->GetWorldMatrix();view.Camera.Position={matrix(0,3),matrix(1,3),matrix(2,3)};view.Camera.Projection=cameraComponent->GetSettings();
                Check(viewport->Render(view),viewport->GetLastError());Check(viewport->GetStatistics().Triangles>0&&viewport->GetStatistics().SkippedObjects==0,viewport->GetLastError());
                std::vector<std::uint8_t> pixels;Check(viewport->ReadPixels(pixels,error),error);
                QImage frame(pixels.data(),960,540,960*4,QImage::Format_RGBA8888);Check(frame.save(QString::fromUtf8(argv[1])),"GPU capture save");
            }catch(...){delete viewport;throw;}
            delete viewport;
        }
        auto idle=animator->GetClipPlaybackState().Clip;Check(!idle.Guid.empty(),"Idle animation never started");
        key(GameSproutRuntime::InputKey::W,true);tick(20);const auto walk=body->GetLinearVelocity();Check(walk.Z>2.8f&&std::fabs(walk.X)<.01f,"W did not move character");Check(animator->GetClipPlaybackState().Clip!=idle,"Animation stayed idle while moving");
        key(GameSproutRuntime::InputKey::D,true);tick(3);const auto diagonal=body->GetLinearVelocity();Check(std::sqrt(diagonal.X*diagonal.X+diagonal.Z*diagonal.Z)<3.1f,"Diagonal movement is faster");key(GameSproutRuntime::InputKey::D,false);
        key(GameSproutRuntime::InputKey::Shift,true);tick(3);const auto run=body->GetLinearVelocity();Check(std::sqrt(run.X*run.X+run.Z*run.Z)>5.7f,"Shift did not run");key(GameSproutRuntime::InputKey::Shift,false);key(GameSproutRuntime::InputKey::W,false);tick(5);
        key(GameSproutRuntime::InputKey::Space,true);runtime->Update(dt);runtime->FixedUpdate(dt);Check(body->GetLinearVelocity().Y>4,"Space did not jump");const float firstJump=body->GetLinearVelocity().Y;runtime->FixedUpdate(dt);Check(body->GetLinearVelocity().Y<firstJump,"Pressed repeated across fixed steps");key(GameSproutRuntime::InputKey::Space,false);runtime->CompleteInputFrame();tick(100);Check(std::fabs(player->GetTransform()->GetLocalPosition().Y-1)<.15f,"Player did not land");
        Check(animator->GetLastError().empty(),animator->GetLastError());Check(animator->GetClipPlaybackState().Clip==idle,"Landing did not return to idle");
        Check(camera->GetTransform()->GetLocalPosition().Z<player->GetTransform()->GetLocalPosition().Z-1,"Camera did not follow");
        GameSproutRuntime::IScriptComponent* cameraScript=nullptr;
        for(size_t i=0;i<camera->GetComponentCount();++i)if(auto* item=dynamic_cast<GameSproutRuntime::IScriptComponent*>(camera->GetComponentAt(i)))cameraScript=item;
        Check(cameraScript,"Camera script missing");
        GameSproutRuntime::RuntimeInputEvent mouse;mouse.Type=GameSproutRuntime::InputEventType::MouseMove;mouse.X=100;mouse.Y=100;Check(runtime->SubmitInputEvent(mouse),"Mouse position");
        mouse.Type=GameSproutRuntime::InputEventType::MouseButtonDown;mouse.Button=GameSproutRuntime::InputMouseButton::Right;Check(runtime->SubmitInputEvent(mouse),"Right button");tick(1);
        mouse.Type=GameSproutRuntime::InputEventType::MouseMove;mouse.X=120;Check(runtime->SubmitInputEvent(mouse),"Mouse delta");tick(1);
        std::map<std::string,GameSproutRuntime::ScriptValue> cameraFields;Check(cameraScript->ReadFields(cameraFields,error),error);
        const auto yaw=cameraFields.at("ThirdPersonCamera::Yaw").Number;Check(yaw>1,"Right mouse did not orbit");
        focus.Type=GameSproutRuntime::InputEventType::FocusLost;Check(runtime->SubmitInputEvent(focus),"Focus lost");mouse.X=500;Check(!runtime->SubmitInputEvent(mouse),"Unfocused mouse input accepted");tick(1);Check(cameraScript->ReadFields(cameraFields,error)&&cameraFields.at("ThirdPersonCamera::Yaw").Number==yaw,"Orbit moved after focus loss");
        const auto playerPosition=player->GetTransform()->GetLocalPosition();
        auto* wall=scene->CreateGameObject("Camera Test Wall");wall->GetTransform()->SetLocalPosition({playerPosition.X,playerPosition.Y+1,playerPosition.Z-2});
        auto* wallCollider=wall->AddComponent<GameSproutRuntime::IBoxColliderComponent>();Check(wallCollider&&wallCollider->SetShape({{10,10,.25f}}),"Camera wall");tick(1);
        Check(camera->GetTransform()->GetLocalPosition().Z>playerPosition.Z-2,"Camera did not pull in before obstruction");
        auto* cameraParent=scene->CreateGameObject("Scaled Camera Parent");
        cameraParent->GetTransform()->SetLocalPosition({1,0,-1});cameraParent->GetTransform()->SetLocalScale({-2,.5f,3});
        Check(cameraParent->GetTransform()->SetLocalRotation({0,std::sin(.3f),0,std::cos(.3f)})&&camera->SetParent(cameraParent,false),"Cannot reparent camera under scaled/rotated parent");
        camera->GetTransform()->SetLocalScale({-1,2,-.5f});tick(2);
        GameSproutRuntime::Mat4 scaledView;
        Check(camera->GetComponent<GameSproutRuntime::ICameraComponent>()->GetViewMatrix(scaledView),"Scaled camera view failed");
        const auto playerWorld=player->GetTransform()->GetWorldMatrix();
        const auto focusPoint=scaledView*GameSproutRuntime::Vector4{playerWorld(0,3),playerWorld(1,3)+.5f,playerWorld(2,3),1};
        Check(std::fabs(focusPoint.X)<.002f&&std::fabs(focusPoint.Y)<.002f&&focusPoint.Z<0,"Camera parent scale changed its world look direction");
        Check(runtime->EndExecution(error),error);Check(Read(project+"/Assets/Playground.scene")==source,"Gameplay changed author source");
        Check(runtime->DestroyScene(scene),"Destroy played world");Check(SceneDocument::Instantiate(baseline,*runtime,scene,ids,error),error);player=Named(scene,"Player");Check(std::fabs(player->GetTransform()->GetLocalPosition().Z)<.001f,"Author snapshot retained Play position");
        const auto prefab=Parse(Read(project+"/Assets/Player.prefab"));Check(PrefabDocument::Validate(prefab["Data"],error),error);
        GameSproutRuntime::IGameObject* clone=nullptr;std::vector<GameSproutRuntime::PrefabObjectMapping> mappings;
        Check(SceneDocument::InstantiatePrefab(prefab["Data"],*runtime,*scene,*scene,clone,mappings,error),error);
        GameSproutRuntime::IScriptComponent* cloneScript=nullptr;
        for(size_t i=0;i<clone->GetComponentCount();++i)if(auto* item=dynamic_cast<GameSproutRuntime::IScriptComponent*>(clone->GetComponentAt(i)))cloneScript=item;
        Check(cloneScript,"Prefab has no character script");
        const auto cloneBody=cloneScript->GetFields().at("ThirdPersonCharacter::Body").Component;
        Check(runtime->QuerySceneComponentByFileID(scene,cloneBody.ComponentFileID)->GetGameObject()==clone,"Prefab Body reference escaped its own instance");
        Check(!cloneScript->GetFields().at("ThirdPersonCharacter::ViewCamera").SceneObject.Assigned,"Prefab retained external scene camera");
        Check(runtime->DestroyScene(scene),"Destroy author/Pefab fixture before wall regression");scene=nullptr;
        GameSproutRuntime::PhysicsMaterialData characterSurface;
        Check(resources.LoadPhysicsMaterial(characterMaterial,characterSurface,error),error);
        Check(characterSurface.Friction==0&&characterSurface.Restitution==0&&characterSurface.FrictionCombine==GameSproutRuntime::PhysicsMaterialCombine::Minimum,
            "Character requires a dedicated zero-friction Minimum-combine material");
        const auto wallJump=[&](const GameSproutRuntime::ResourceReference& material,bool running,bool sliding) {
            Check(SceneDocument::Instantiate(baseline,*runtime,scene,ids,error),error);
            auto* actor=Named(scene,"Player");auto* actorBody=actor->GetComponent<GameSproutRuntime::IRigidbodyComponent>();
            auto* capsule=actor->GetComponent<GameSproutRuntime::ICapsuleColliderComponent>();
            auto settings=capsule->GetColliderSettings();settings.Material=material;
            Check(capsule->SetColliderSettings(settings),"Set per-case character material");
            GameSproutRuntime::IScriptComponent* actorScript=nullptr;
            for(size_t i=0;i<actor->GetComponentCount();++i)if(auto* item=dynamic_cast<GameSproutRuntime::IScriptComponent*>(actor->GetComponentAt(i)))actorScript=item;
            Check(actorScript,"Wall fixture character script missing");
            auto cameraReference=actorScript->GetFields().at("ThirdPersonCharacter::ViewCamera");cameraReference.SceneObject={};
            Check(actorScript->SetField("ThirdPersonCharacter::ViewCamera",cameraReference,error),error);
            auto* contactWall=scene->CreateGameObject("Wall Friction Regression");
            contactWall->GetTransform()->SetLocalPosition({.75f,3,0});
            auto* contactCollider=contactWall->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
            Check(contactCollider&&contactCollider->SetShape({{.5f,6,20}}),"Wall regression shape");
            Check(contactCollider->GetColliderSettings().Material.Guid.empty(),"Regression must retain the ordinary wall surface");
            Check(runtime->BeginExecution(error),error);
            Check(actorBody->Teleport({.08f,1.02f,0},{},true),"Position capsule next to wall");
            focus.Type=GameSproutRuntime::InputEventType::FocusGained;Check(runtime->SubmitInputEvent(focus),"Wall fixture focus");
            tick(30);
            key(GameSproutRuntime::InputKey::Space,true);runtime->Update(dt);runtime->FixedUpdate(dt);runtime->CompleteInputFrame();
            key(GameSproutRuntime::InputKey::Space,false);tick(14);
            const float apexStart=actor->GetTransform()->GetLocalPosition().Y;
            Check(apexStart>1.7f,"Wall regression failed to jump into the air");
            key(GameSproutRuntime::InputKey::D,true);
            if(running)key(GameSproutRuntime::InputKey::Shift,true);
            if(sliding)key(GameSproutRuntime::InputKey::W,true);
            tick(100);
            const auto held=actor->GetTransform()->GetLocalPosition();
            if(material.Guid.empty()) {
                Check(held.Y>1.7f,"Ordinary friction no longer reproduces the reported wall hold");
            } else {
                Check(std::fabs(held.Y-1)<.15f,"Character remained suspended while pushing into the wall");
                if(sliding)Check(held.Z>1.5f,"Character could not slide along the wall");
            }
            key(GameSproutRuntime::InputKey::D,false);
            if(running)key(GameSproutRuntime::InputKey::Shift,false);
            if(sliding)key(GameSproutRuntime::InputKey::W,false);
            tick(60);
            const float released=actor->GetTransform()->GetLocalPosition().Y;
            Check(std::fabs(released-1)<.15f,"Character failed to land after input release");
            Check(actorScript->GetScriptError().empty(),actorScript->GetScriptError());
            std::cout<<"Wall material="<<(material.Guid.empty()?"default":"character")<<" running="<<running<<" sliding="<<sliding
                <<" jumpY="<<apexStart<<" heldY="<<held.Y<<" heldZ="<<held.Z<<" releasedY="<<released<<'\n';
            Check(runtime->EndExecution(error),error);Check(runtime->DestroyScene(scene),"Release wall fixture");scene=nullptr;
        };
        wallJump({},false,false);
        wallJump(characterMaterial,false,false);
        wallJump(characterMaterial,true,false);
        wallJump(characterMaterial,false,true);
        delete runtime;runtime=nullptr;for(auto* job:jobs)delete job;jobs.clear();Check(logic.unload(),"Native module stayed leased");
        std::cout<<"PASS real reflection/native DLL, FBX skeleton + four clips import, scene refs, ground/move/run/diagonal/jump/land, animation, camera, source isolation, snapshot restore and wall-friction regression\n";return 0;
    }catch(const std::exception& exception){std::cerr<<"FAIL "<<exception.what()<<'\n';delete runtime;for(auto* job:jobs)delete job;return 1;}
}
