#include "SceneViewport.h"
#include "SceneViewportEditing.h"
#include "GameViewport.h"
#include "GameSproutRuntimeExport.h"
#include "../Shared/ViewportRuntimeSpy.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QExposeEvent>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QThread>
#include <QWheelEvent>
#include <QWindow>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
void Check(bool result,const std::string& message){if(!result)throw std::runtime_error(message);}
void SendMouse(QWidget& widget,QEvent::Type type,const QPoint& position,Qt::MouseButton button,Qt::MouseButtons buttons) {
    QMouseEvent event(type,QPointF(position),QPointF(widget.mapToGlobal(position)),button,buttons,Qt::NoModifier);
    QCoreApplication::sendEvent(&widget,&event);
}
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
    bool SkipNextFrame=false;
    bool ReuseShadowsWhileNavigating=false;
    bool Render(const GameSproutRuntime::RenderView& view) override {
        Check(!view.WaitForSurface,"Editor view requested a blocking surface acquisition");
        ++Count;
        ReuseShadowsWhileNavigating=view.ReuseShadowsWhileNavigating;
        Camera={};
        if(SkipNextFrame) {
            SkipNextFrame=false;Statistics.Width=0;Statistics.Height=0;
            return true;
        }
        Statistics.Width=640;Statistics.Height=360;
        Camera.Camera=view.Camera;Camera.Width=Statistics.Width;Camera.Height=Statistics.Height;
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
        std::vector<CountingViewport*> renderers;
        spy.ViewportFactory=[&](const auto& window,auto*,std::string&)->GameSproutRuntime::IRuntimeViewport* {
            handles.push_back(window.Handle);auto* renderer=new CountingViewport(counts[window.Handle]);
            renderers.push_back(renderer);return renderer;
        };
        {
            std::uint64_t sceneSubmissions=0,gameSubmissions=0;
            SceneViewport scene;GameViewport game;
            QObject::connect(&scene,&SceneViewport::FrameSubmitted,&scene,[&]{++sceneSubmissions;});
            QObject::connect(&game,&GameViewport::FrameSubmitted,&game,[&]{++gameSubmissions;});
            scene.setWindowTitle("Scene scheduling check");game.setWindowTitle("Game scheduling check");
            scene.resize(640,360);game.resize(640,360);
            scene.Initialize(&spy,&resources);game.Initialize(&spy,&resources);
            scene.show();game.show();QCoreApplication::processEvents();
            const auto tick=[&](bool continuous=false,bool animateMaterials=false){QCoreApplication::processEvents();runtime->Update(0);scene.RenderScene(world,true,1.f/60,continuous,animateMaterials);game.RenderGame(world,continuous,animateMaterials);};
            for(unsigned index=0;index<24;++index)tick();
            Check(scene.IsRenderingAvailable()&&game.IsRenderingAvailable()&&handles.size()>=2,"Native counting viewports did not initialize");
            auto* sceneRenderer=dynamic_cast<CountingViewport*>(scene.Renderer());Check(sceneRenderer,"Scene did not use Runtime viewport factory");
            const auto sceneHandle=handles.front();const auto gameHandle=handles[1];
            const auto idleScene=counts[sceneHandle],idleGame=counts[gameHandle];
            for(unsigned index=0;index<120;++index)tick();
            Check(counts[sceneHandle]==idleScene&&counts[gameHandle]==idleGame,"Static edit frames continued submitting draws");
            for(auto* renderer:renderers)renderer->Statistics.RequiresContinuousRendering=true;
            scene.RequestRender();game.RequestRender();tick();
            auto animatedScene=counts[sceneHandle],animatedGame=counts[gameHandle];
            for(unsigned index=0;index<4;++index)tick();
            Check(counts[sceneHandle]==animatedScene&&counts[gameHandle]==animatedGame,"Time-dependent materials redrew without explicit preview");
            for(unsigned index=0;index<4;++index)tick(false,true);
            Check(counts[sceneHandle]==animatedScene+4&&counts[gameHandle]==animatedGame+4,"Explicit material preview did not redraw");
            animatedScene=counts[sceneHandle];animatedGame=counts[gameHandle];
            for(unsigned index=0;index<4;++index)tick(false,false);
            Check(counts[sceneHandle]==animatedScene&&counts[gameHandle]==animatedGame,"Paused gameplay kept drawing frozen material time");
            for(auto* renderer:renderers)renderer->Statistics.RequiresContinuousRendering=false;
            tick(false,true);animatedScene=counts[sceneHandle];animatedGame=counts[gameHandle];
            for(unsigned index=0;index<4;++index)tick(false,true);
            Check(counts[sceneHandle]==animatedScene&&counts[gameHandle]==animatedGame,"Removing the last animated material did not return viewports to idle");
            cameraObject->SetName("Renamed Camera");tick();
            Check(counts[sceneHandle]==animatedScene&&counts[gameHandle]==animatedGame,"Author metadata triggered a visual redraw");
            const auto submittedScene=sceneSubmissions,submittedGame=gameSubmissions;
            for(auto* renderer:renderers)renderer->SkipNextFrame=true;
            scene.RequestRender();game.RequestRender();tick();
            Check(sceneSubmissions==submittedScene&&gameSubmissions==submittedGame,"Busy surfaces counted as submitted FPS frames");
            GameSproutRuntime::RenderedCamera presented;
            Check(scene.GetRenderedCamera(presented)&&presented.SceneID==world->GetInstanceID(),"Busy surface discarded the last presented picking camera");
            tick();
            Check(counts[sceneHandle]==animatedScene+2&&counts[gameHandle]==animatedGame+2,"Busy surface dropped a pending redraw");
            Check(sceneSubmissions==submittedScene+1&&gameSubmissions==submittedGame+1,"Successful retries did not reach both FPS counters");
            cameraObject->GetTransform()->SetLocalPosition({1,2,3});tick();
            Check(counts[sceneHandle]>idleScene&&counts[gameHandle]>idleGame,"Runtime visual change did not redraw both viewports");
            auto previousScene=counts[sceneHandle],previousGame=counts[gameHandle];
            for(unsigned index=0;index<16;++index)tick();
            Check(counts[sceneHandle]==previousScene&&counts[gameHandle]==previousGame,"A property edit kept an idle redraw loop alive");
            scene.Camera().Dolly(1);tick();
            Check(counts[sceneHandle]>previousScene&&counts[gameHandle]==previousGame,"Scene camera change did not stay viewport-local");
            auto* gameRenderer=renderers[1];
            scene.raise();scene.activateWindow();QApplication::setActiveWindow(&scene);scene.setFocus();tick();
            const auto navigate=[&] {
                const auto before=counts[sceneHandle];
                SendMouse(scene,QEvent::MouseButtonPress,{100,100},Qt::RightButton,Qt::RightButton);
                SendMouse(scene,QEvent::MouseMove,{130,110},Qt::NoButton,Qt::RightButton);
                tick();
                Check(counts[sceneHandle]>before&&sceneRenderer->ReuseShadowsWhileNavigating,
                    "Free camera navigation did not request shadow atlas reuse");
            };
            const auto checkFinalShadowDraw=[&](const char* message) {
                const auto before=counts[sceneHandle];
                tick();
                Check(counts[sceneHandle]>before&&!sceneRenderer->ReuseShadowsWhileNavigating,message);
            };
            navigate();
            scene.SetShadowReuseWhileNavigatingEnabled(false);
            checkFinalShadowDraw("Disabling reuse during a drag did not request an exact shadow fit");
            scene.SetShadowReuseWhileNavigatingEnabled(true);
            const auto beforeReenable=counts[sceneHandle];
            tick();
            Check(counts[sceneHandle]>beforeReenable&&sceneRenderer->ReuseShadowsWhileNavigating,
                "Enabling reuse during a drag did not resume the navigation policy");
            game.RequestRender();tick();
            Check(!gameRenderer->ReuseShadowsWhileNavigating,"Scene camera navigation altered Game shadow policy");
            SendMouse(scene,QEvent::MouseButtonRelease,{130,110},Qt::RightButton,Qt::NoButton);
            checkFinalShadowDraw("Releasing free camera did not request an exact shadow fit");
            navigate();
            QFocusEvent lostFocus(QEvent::FocusOut);QCoreApplication::sendEvent(&scene,&lostFocus);
            checkFinalShadowDraw("Losing free camera focus did not request an exact shadow fit");
            const auto scroll=[&](int delta,bool pixelDelta=false) {
                const QPoint position(100,100);
                const QPoint offset(0,delta);
                QWheelEvent event(QPointF(position),QPointF(scene.mapToGlobal(position)),pixelDelta?offset:QPoint(),
                    pixelDelta?QPoint():offset,Qt::NoButton,Qt::NoModifier,
                    pixelDelta?Qt::ScrollUpdate:Qt::NoScrollPhase,false);
                QCoreApplication::sendEvent(&scene,&event);
            };
            for(int delta:{120,-120,120}) {
                const auto before=counts[sceneHandle];
                scroll(delta,delta<0);tick();
                Check(counts[sceneHandle]==before+1&&sceneRenderer->ReuseShadowsWhileNavigating,
                    "Wheel navigation did not reuse shadows in both directions");
            }
            const auto lastWheelDraw=counts[sceneHandle];
            tick();
            Check(counts[sceneHandle]==lastWheelDraw,"Active wheel timeout caused continuous drawing");
            QElapsedTimer settle;
            settle.start();
            while(counts[sceneHandle]==lastWheelDraw&&settle.elapsed()<1000) {
                QThread::msleep(5);
                tick();
            }
            Check(counts[sceneHandle]==lastWheelDraw+1&&!sceneRenderer->ReuseShadowsWhileNavigating,
                "Wheel inactivity did not request one exact shadow fit");
            tick();
            Check(counts[sceneHandle]==lastWheelDraw+1,"Wheel settle kept drawing after the exact fit");
            scene.SetShadowReuseWhileNavigatingEnabled(false);tick();
            scroll(-120);
            checkFinalShadowDraw("Wheel navigation reused shadows with reuse disabled");
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
