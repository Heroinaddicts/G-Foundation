#include "GameViewport.h"
#include "SceneViewport.h"
#include "GameSproutRuntimeExport.h"
#include "RuntimeResourceApi.h"
#include "../Shared/ViewportRuntimeSpy.h"
#include "ui_GameViewUI.h"
#include "ui_SceneViewUI.h"
#include <QApplication>
#include <QPlatformSurfaceEvent>
#include <QWindow>
#include <QElapsedTimer>
#include <QThread>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value,const std::string& text){if(!value)throw std::runtime_error(text);}
class Resources final : public GameSproutRuntime::IRuntimeResourceProvider {
public:
    int RenderLoads=0;
    std::uint64_t GetRevision()const override{return 1;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MeshRenderData&,std::string&)override{++RenderLoads;return false;}
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData&,std::string&)override{++RenderLoads;return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData&,std::string&)override{++RenderLoads;return false;}
    bool LoadSkeleton(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::SkeletonData& output,std::string&)override{
        output={};GameSproutRuntime::SkeletonNode bone;bone.Path="/Bone";bone.Name="Bone";output.Nodes.push_back(bone);return true;
    }
    bool LoadAnimationClip(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::AnimationClipData& output,std::string&)override{
        output={};output.Duration=1;output.SampleRate=1;output.Tracks={"/Bone"};
        GameSproutRuntime::AnimationFrame first,last;first.Time=0;last.Time=1;first.Poses.push_back({});last.Poses.push_back({});last.Poses[0].Position.X=1;
        output.Frames={first,last};return true;
    }
};
struct RuntimeOwner {
    GameSproutRuntime::IGameSproutRuntime* Pointer=nullptr;
    ~RuntimeOwner(){delete Pointer;}
};
void SurfaceEvent(QObject* object,QPlatformSurfaceEvent::SurfaceEventType type) {
    if(!object)return;QPlatformSurfaceEvent event(type);QCoreApplication::sendEvent(object,&event);
}
}
int main(int argc,char** argv) {
    QApplication application(argc,argv);
    try {
        const bool native=QCoreApplication::arguments().contains("--native");
        if(native&&SceneViewport::IsHeadless()){std::cout<<"SKIP native test requires a desktop Qt platform\n";return 77;}
        Check(native||SceneViewport::IsHeadless(),"Run headless test with QT_QPA_PLATFORM=offscreen");
        Resources resources;ViewportRuntimeSpy boundary;boundary.RejectNativeCalls=!native;
        RuntimeOwner owner;owner.Pointer=Export(TEST_RUNTIME_ROOT);Check(owner.Pointer,"Cannot load Runtime SDK");
        boundary.Real=owner.Pointer;auto* runtime=&boundary;runtime->SetResourceProvider(&resources);auto* scene=runtime->CreateScene("Shared Editor scene");
        auto* rig=scene->CreateGameObject("Rig");auto* bone=scene->CreateGameObject("Bone",rig);
        auto* player=rig->AddComponent<GameSproutRuntime::IAnimatorComponent>();Check(player,"Animator factory unavailable");
        player->SetSkeleton({"11111111111111111111111111111111",1});player->SetDefaultClip({"11111111111111111111111111111111",2});
        Check(player->SetNodeBindings({{"/Bone",bone->GetInstanceID()}})&&player->Play(),player->GetLastError());
        auto* cameraObject=scene->CreateGameObject("Game Main Camera");cameraObject->GetTransform()->SetLocalPosition({0,0,3});
        auto* camera=cameraObject->AddComponent<GameSproutRuntime::ICameraComponent>();Check(scene->SetMainCamera(camera),"Cannot configure Main Camera");
        runtime->Update(.2f);const float time=player->GetPlaybackState().Time;
        Check(std::fabs(time-.2f)<.0001f,"Module-owned update did not advance animation");

        bool gameDestroyed=false,sceneDestroyed=false;std::string status;
        auto* gameWindow=new QWidget();auto* sceneWindow=new QWidget();
        Ui::GameViewUIWindow gameForm;gameForm.setupUi(gameWindow);
        Ui::SceneViewUIWindow sceneForm;sceneForm.setupUi(sceneWindow);
        auto* game=gameForm.gameViewport;auto* sceneView=sceneForm.sceneViewport;
        QObject::connect(game,&QObject::destroyed,[&]{gameDestroyed=true;});
        QObject::connect(sceneView,&QObject::destroyed,[&]{sceneDestroyed=true;});
        Check(gameWindow->findChild<QToolButton*>("playButton")&&gameWindow->findChild<QToolButton*>("stopButton")&&
            sceneWindow->findChild<QToolButton*>("focusSelectionButton"),"Promoted View controls were not loaded from their .ui files");
        game->StatusChanged=[&](const std::string& text){status=text;};
        game->Initialize(runtime,&resources);sceneView->Initialize(runtime,&resources);
        gameWindow->resize(640,360);sceneWindow->resize(640,360);gameWindow->show();sceneWindow->show();application.processEvents();
        const auto awaitNative=[&]{
            if(!native)return;QElapsedTimer timer;timer.start();
            while(timer.elapsed()<5000){application.processEvents();game->RenderGame(scene);sceneView->RenderScene(scene,true,.1f);
                if(game->IsRenderingAvailable()&&sceneView->IsRenderingAvailable())return;QThread::msleep(8);}
            throw std::runtime_error("Native Scene/Game rendering did not initialize: "+status+" | "+sceneView->GetRenderError());
        };
        awaitNative();
        for(int frame=0;frame<8;++frame) {
            game->RenderGame(scene);sceneView->RenderScene(scene,true,.25f);application.processEvents();
        }
        Check(player->GetPlaybackState().Time==time,"Scene/Game Views advanced the Module-owned animation clock");
        Check(boundary.NativeCalls==0&&resources.RenderLoads==0,"Headless View touched native GPU/resource rendering");
        Check(native||status.find("Main Camera")!=std::string::npos,"Game headless state did not identify the game camera");
        Check(scene->SetMainCamera(nullptr),"Cannot remove Main Camera");game->RenderGame(scene);
        Check(status.find("Main Camera")!=std::string::npos,"Missing Main Camera did not submit an empty view with its diagnostic");
        game->RenderGame(nullptr);Check(status.find("活动场景")!=std::string::npos,"Missing scene retained the previous game view");
        Check(scene->SetMainCamera(camera),"Cannot restore Main Camera");game->RenderGame(scene);
        const auto gameWorld=cameraObject->GetTransform()->GetWorldMatrix();auto freeCamera=sceneView->Camera().GetState();
        freeCamera.Position={10,20,30};Check(sceneView->Camera().SetState(freeCamera),"Cannot move independent Scene camera");
        for(std::size_t component=0;component<16;++component)Check(gameWorld.Data[component]==cameraObject->GetTransform()->GetWorldMatrix().Data[component],"Scene camera changed Main Camera");

        // QWidget and QWindow destruction/recreation events must be harmless before/after shutdown.
        game->winId();sceneView->winId();application.processEvents();
        if(native){
            Check(gameWindow->windowHandle(),"Game host has no native QWindow");
            gameWindow->windowHandle()->destroy();
            Check(!game->IsRenderingAvailable(),"Host platform surface was destroyed before Game released its borrowed Surface");
            Check(sceneView->IsRenderingAvailable(),"Game host teardown destroyed Scene's independent GPU state");
        }else{
            SurfaceEvent(game->windowHandle(),QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed);
            SurfaceEvent(gameWindow->windowHandle(),QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed);
            SurfaceEvent(game->windowHandle(),QPlatformSurfaceEvent::SurfaceCreated);
        }
        game->RetryRendering();game->RenderGame(scene);
        gameWindow->hide();sceneWindow->hide();application.processEvents();gameWindow->show();sceneWindow->show();application.processEvents();
        awaitNative();
        game->RenderGame(scene);sceneView->RenderScene(scene,false,1);
        Check(player->GetPlaybackState().Time==time&&boundary.NativeCalls==0,"Window reopen advanced animation or created a native GPU in headless mode");
        game->Shutdown();game->Shutdown();sceneView->Shutdown();sceneView->Shutdown();
        game->RenderGame(scene);sceneView->RenderScene(scene,false,1);
        if(!native)SurfaceEvent(gameWindow->windowHandle(),QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed);
        game->Initialize(runtime,&resources);sceneView->Initialize(runtime,&resources);
        awaitNative();
        game->RenderGame(scene);sceneView->RenderScene(scene,true,.5f);
        Check(player->GetPlaybackState().Time==time&&boundary.NativeCalls==0,"Reinitialize changed scheduling or reused invalid GPU state");
        delete gameWindow;delete sceneWindow;
        Check(gameDestroyed&&sceneDestroyed,"View host deletion did not destroy its promoted viewport");
        Check(player->GetPlaybackState().Time==time&&runtime->GetSceneCount()==1,"View teardown owned/destroyed shared Runtime data");
        std::cout<<"PASS .ui promotion, shared Module resources, Scene/Game clock and camera isolation, host/native surface lifecycle\n";
        return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
