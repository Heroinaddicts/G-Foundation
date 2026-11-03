#include "SceneViewport.h"
#include "SceneViewportEditing.h"
#include "GameViewport.h"
#include "GameSproutRuntimeExport.h"
#include "../Shared/ViewportRuntimeSpy.h"
#include <QApplication>
#include <QExposeEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QWindow>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
void Check(bool result,const std::string& message){if(!result)throw std::runtime_error(message);}
const std::string ResourceGuid="11111111111111111111111111111111";
class Resources final:public GameSproutRuntime::IRuntimeResourceProvider {
public:
    std::uint64_t GetRevision() const override{return 1;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MeshRenderData&,std::string&) override{return false;}
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData&,std::string&) override{return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData&,std::string&) override{return false;}
    bool LoadSkeleton(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::SkeletonData& value,std::string&) override {
        value={};value.Nodes={{"/bone","Bone",-1,{}}};return true;
    }
    bool LoadAnimationClip(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::AnimationClipData& value,std::string&) override {
        value={};value.Duration=1;value.SampleRate=1;value.Tracks={"/bone"};
        GameSproutRuntime::AnimationFrame first,last;first.Time=0;last.Time=1;first.Poses.resize(1);last.Poses.resize(1);
        last.Poses[0].Position.X=4;value.Frames={first,last};return true;
    }
};
// A real native QWidget exercises the View's scheduling and native event path;
// this counting renderer avoids relying on screenshots or a GPU timing result.
class CountingViewport final:public GameSproutRuntime::IRuntimeViewport {
public:
    explicit CountingViewport(std::uint64_t& count):Count(count) {Statistics.Width=640;Statistics.Height=360;}
    std::uint64_t& Count;
    GameSproutRuntime::RenderedCamera Camera;
    GameSproutRuntime::RenderStatistics Statistics;
    std::string Error;
    bool Render(const GameSproutRuntime::RenderView& view) override {
        ++Count;Camera={};Camera.Camera=view.Camera;Camera.Width=Statistics.Width;Camera.Height=Statistics.Height;
        Camera.SceneID=view.Scene?view.Scene->GetInstanceID():0;return true;
    }
    const std::string& GetLastError() const override{return Error;}
    const GameSproutRuntime::RenderStatistics& GetStatistics() const override{return Statistics;}
    bool GetRenderedCamera(GameSproutRuntime::RenderedCamera& output) const override{output=Camera;return Camera.SceneID!=0;}
    bool Raycast(GameSproutRuntime::IScene*,const GameSproutRuntime::Ray&,GameSproutRuntime::SceneRaycastHit&,std::uint32_t) override{return false;}
    bool GetWorldBounds(GameSproutRuntime::IScene*,std::uint64_t,GameSproutRuntime::Bounds&) override{return false;}
    bool AppendSelectionOutline(GameSproutRuntime::IScene*,std::uint64_t,GameSproutRuntime::DebugDrawList&,const GameSproutRuntime::Color&,bool) override{return false;}
    bool AppendColliderWireframes(GameSproutRuntime::IScene*,const GameSproutRuntime::ColliderDebugOptions&,GameSproutRuntime::DebugDrawList&) override{return true;}
    bool ReadPixels(std::vector<std::uint8_t>&,std::string&) override{return false;}
    bool Resize(std::uint32_t,std::uint32_t,std::string&) override{return true;}
};
}
int main(int argc,char** argv) {
    QApplication application(argc,argv);
    if(SceneViewport::IsHeadless()){std::cout<<"SKIP native render scheduling requires a desktop Qt platform\n";return 0;}
    auto* runtime=Export(TEST_RUNTIME_ROOT);
    if(!runtime){std::cerr<<"FAIL cannot load Runtime\n";return 1;}
    try {
        Resources resources;runtime->SetResourceProvider(&resources);
        auto* world=runtime->CreateScene("Isolated viewport scheduling");auto* cameraObject=world->CreateGameObject("Camera");
        auto* camera=cameraObject->AddComponent<GameSproutRuntime::ICameraComponent>();Check(world->SetMainCamera(camera),"Cannot set fixture camera");
        auto* model=world->CreateGameObject("Model");auto* bone=world->CreateGameObject("Bone",model);
        auto* animator=model->AddComponent<GameSproutRuntime::IAnimatorComponent>();animator->SetSkeleton({ResourceGuid,10});
        Check(animator->SetNodeBindings({{"/bone",bone->GetInstanceID()}}),"Cannot bind fixture animation");
        ViewportRuntimeSpy spy;spy.Real=runtime;std::map<std::uintptr_t,std::uint64_t> counts;
        std::vector<std::uintptr_t> handles;
        spy.ViewportFactory=[&](const auto& window,auto*,std::string&)->GameSproutRuntime::IRuntimeViewport* {
            handles.push_back(window.Handle);return new CountingViewport(counts[window.Handle]);
        };
        {
            SceneViewport scene;GameViewport game;
            scene.setWindowTitle("Scene scheduling check");game.setWindowTitle("Game scheduling check");
            scene.resize(640,360);game.resize(640,360);
            scene.Initialize(&spy,&resources);game.Initialize(&spy,&resources);
            scene.show();game.show();QCoreApplication::processEvents();
            const auto tick=[&](bool continuous=false){QCoreApplication::processEvents();runtime->Update(0);scene.RenderScene(world,true,1.f/60,continuous);game.RenderGame(world,continuous);};
            for(unsigned index=0;index<24;++index)tick();
            Check(scene.IsRenderingAvailable()&&game.IsRenderingAvailable()&&handles.size()>=2,"Native counting viewports did not initialize");
            auto* sceneRenderer=dynamic_cast<CountingViewport*>(scene.Renderer());Check(sceneRenderer,"Scene did not use Runtime viewport factory");
            const auto sceneHandle=handles.front();const auto gameHandle=handles[1];
            const auto idleScene=counts[sceneHandle],idleGame=counts[gameHandle];
            for(unsigned index=0;index<120;++index)tick();
            Check(counts[sceneHandle]==idleScene&&counts[gameHandle]==idleGame,"Static edit frames continued submitting draws");
            cameraObject->GetTransform()->SetLocalPosition({1,2,3});tick();
            Check(counts[sceneHandle]>idleScene&&counts[gameHandle]>idleGame,"Runtime visual change did not redraw both viewports");
            auto previousScene=counts[sceneHandle],previousGame=counts[gameHandle];
            for(unsigned index=0;index<16;++index)tick();
            Check(counts[sceneHandle]==previousScene&&counts[gameHandle]==previousGame,"A property edit kept an idle redraw loop alive");
            scene.Camera().Dolly(1);tick();
            Check(counts[sceneHandle]>previousScene&&counts[gameHandle]==previousGame,"Scene camera change did not stay viewport-local");
            previousScene=counts[sceneHandle];scene.Editing().SetShowAllColliders(true);tick();
            Check(counts[sceneHandle]>previousScene,"Collider display toggle did not redraw");
            previousScene=counts[sceneHandle];scene.Editing().SetTool(SceneEditingTool::Rotate);tick();
            Check(counts[sceneHandle]>previousScene,"Tool change did not redraw");
            previousScene=counts[sceneHandle];previousGame=counts[gameHandle];
            for(unsigned index=0;index<4;++index)tick(true);
            Check(counts[sceneHandle]==previousScene+4&&counts[gameHandle]==previousGame+4,"Continuous play/preview did not draw every host frame");
            previousScene=counts[sceneHandle];previousGame=counts[gameHandle];tick();
            Check(counts[sceneHandle]==previousScene&&counts[gameHandle]==previousGame,"Pausing continued drawing without a change");
            Check(animator->PlayClip({ResourceGuid,1},.2f)&&animator->SetClipPlaybackPlaying(false),animator->GetLastError());
            for(unsigned index=0;index<4;++index)tick();
            previousScene=counts[sceneHandle];previousGame=counts[gameHandle];
            Check(animator->SeekClip(.75f),animator->GetLastError());tick();
            Check(counts[sceneHandle]>previousScene&&counts[gameHandle]>previousGame,"Paused animation seek did not redraw its new pose");
            previousScene=counts[sceneHandle];previousGame=counts[gameHandle];
            for(unsigned index=0;index<32;++index)tick();
            Check(counts[sceneHandle]==previousScene&&counts[gameHandle]==previousGame,"Paused animation kept invalidating a static pose");
            // A single paused gameplay step is one explicitly requested host frame.
            tick(true);const auto steppedScene=counts[sceneHandle],steppedGame=counts[gameHandle];tick();
            Check(counts[sceneHandle]==steppedScene&&counts[gameHandle]==steppedGame,"Paused single-step did not return to idle");
            QExposeEvent expose(QRegion(scene.rect()));QCoreApplication::sendEvent(scene.windowHandle(),&expose);tick();
            Check(counts[sceneHandle]>steppedScene,"Native Expose did not wake the retained frame");
            previousGame=counts[gameHandle];game.hide();QCoreApplication::processEvents();game.show();
            for(unsigned index=0;index<4;++index)tick();
            Check(counts[gameHandle]>previousGame,"Hidden Game viewport did not redraw when shown");
            previousScene=counts[sceneHandle];scene.resize(660,380);
            for(unsigned index=0;index<4;++index)tick();
            Check(counts[sceneHandle]>previousScene,"Resize did not wake Scene rendering");
            previousGame=counts[gameHandle];game.RequestRender();tick();
            Check(counts[gameHandle]>previousGame,"Explicit resource/loading invalidation did not wake Game rendering");
            scene.Shutdown();game.Shutdown();
            Check(runtime->DestroyScene(world),"Cannot destroy isolated scene after listener teardown");runtime->ProcessPendingEvents();
        }
        runtime->SetResourceProvider(nullptr);delete runtime;runtime=nullptr;
        std::cout<<"PASS idle viewport scheduling, visual changes, camera/tools, paused seek/step, expose/show/resize and listener teardown\n";return 0;
    }catch(const std::exception& exception){runtime->SetResourceProvider(nullptr);delete runtime;std::cerr<<"FAIL "<<exception.what()<<'\n';return 1;}
}
