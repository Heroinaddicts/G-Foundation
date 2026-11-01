#include "GameSproutRuntimeExport.h"
#include "RuntimeRenderApi.h"
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

static void Check(bool condition,const std::string& message) {if(!condition) throw std::runtime_error(message);}
static void Float(std::vector<std::uint8_t>& bytes,float value) {
    std::uint8_t data[4];std::memcpy(data,&value,4);bytes.insert(bytes.end(),data,data+4);
}
class Provider final : public GameSproutRuntime::IRuntimeResourceProvider {
public:
    std::uint64_t Revision=1;
    int MeshLoads=0,TextureLoads=0,MaterialLoads=0;
    bool UV=true,Color=true,SRGB=true,BadIndex=false,Missing=false,DiagonalNormal=false,MissingMaterial=false,MissingTexture=false;
    std::uint8_t Green=255;
    std::uint64_t GetRevision() const override {return Revision;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MeshRenderData& data,std::string& error) override {
        ++MeshLoads;data={};
        if(Missing) {error="Missing prepared mesh fixture";return false;}
        data.VertexCount=3;data.VertexStride=24+(Color?16:0)+(UV?8:0);data.ColorOffset=Color?24:-1;data.UVOffset=UV?(Color?40:24):-1;
        const float positions[3][3]={{-1,-1,0},{1,-1,0},{0,1,0}};
        for(const auto& position:positions) {
            for(float value:position) Float(data.Bytes,value);
            Float(data.Bytes,DiagonalNormal?1.0f:0.0f);Float(data.Bytes,DiagonalNormal?1.0f:0.0f);Float(data.Bytes,DiagonalNormal?0.0f:1.0f);
            if(Color) for(int i=0;i<4;++i) Float(data.Bytes,1);
            if(UV) {Float(data.Bytes,0.5f);Float(data.Bytes,0.5f);}
        }
        data.IndexOffset=data.Bytes.size();data.IndexCount=3;
        for(std::uint32_t index:{0u,1u,BadIndex?99u:2u}) {
            data.Bytes.push_back(std::uint8_t(index));data.Bytes.push_back(0);data.Bytes.push_back(0);data.Bytes.push_back(0);
        }
        data.LocalBounds={{0,0,0},{1,1,0}};return true;
    }
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData& data,std::string& error) override {
        ++TextureLoads;data={};data.Width=2;data.Height=2;data.SRGB=SRGB;
        if(MissingTexture) {data={};error="Missing prepared texture fixture";return false;}
        for(int i=0;i<4;++i) data.Pixels.insert(data.Pixels.end(),{0,Green,0,255});return true;
    }
    bool LoadMaterial(const GameSproutRuntime::ResourceReference& reference,GameSproutRuntime::MaterialRenderData& data,std::string& error) override {
        ++MaterialLoads;data={};
        if(MissingMaterial) {error="Missing prepared material fixture";return false;}
        if(reference.LocalID==2) data.BaseColorTexture={"0123456789abcdef0123456789abcdef",3};
        if(reference.LocalID==4) data.BaseColor={1,0,0,1};
        return true;
    }
};
class Probe final : public GameSproutRuntime::IRuntimeListener {
public:
    int Calls=0;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent&) override {++Calls;}
};
static std::array<int,4> Center(GameSproutRuntime::IRuntimeViewport& renderer) {
    std::vector<std::uint8_t> pixels;std::string error;
    Check(renderer.ReadPixels(pixels,error),error);
    const auto& extent=renderer.GetStatistics();
    const auto offset=(std::size_t(extent.Height/2)*extent.Width+extent.Width/2)*4;
    Check(pixels.size()==std::size_t(extent.Width)*extent.Height*4,"Readback extent mismatch");
    return {pixels[offset],pixels[offset+1],pixels[offset+2],pixels[offset+3]};
}
#include "RuntimeSceneToolsChecks.h"
int main(int argc,char** argv) {
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    GameSproutRuntime::IRuntimeViewport* renderer=nullptr;
    std::string error;
    Provider provider;
    try {
        Check(argc==2,"Pass the SDK binary directory");
        runtime=Export(argv[1]);Check(runtime,"Cannot load real Runtime SDK");
        renderer=runtime->CreateOffscreenRenderer(128,128,&provider,error);
        if(!renderer){delete runtime;std::cout<<"SKIP Runtime GPU unavailable: "<<error<<'\n';return 77;}
        Check(!runtime->CreateOffscreenRenderer(128,128,nullptr,error),"Invalid viewport dependencies accepted");
        Check(!runtime->CreateViewport({},&provider,error),"Invalid native window accepted");
        auto* second=runtime->CreateOffscreenRenderer(16,16,&provider,error);Check(second,error);delete second;
        GameSproutRuntime::RenderView startupClear;startupClear.DrawScene=false;startupClear.ClearColor={1,0,0,1};
        Check(renderer->Render(startupClear),renderer->GetLastError());
        const auto startupPixel=Center(*renderer);
        Check(startupPixel[0]==255&&startupPixel[1]==0&&startupPixel[2]==0&&provider.MeshLoads==0,
            "An empty startup view required a scene or loaded draw resources");
        auto* scene=runtime->CreateScene("Offscreen scene");auto* object=scene->CreateGameObject("Real triangle");
        auto* mesh=object->AddComponent<GameSproutRuntime::IMeshComponent>();
        auto* meshRenderer=object->AddComponent<GameSproutRuntime::IMeshRendererComponent>();
        mesh->SetMesh({"0123456789abcdef0123456789abcdef",1});
        Check(meshRenderer->SetMaterials({{"0123456789abcdef0123456789abcdef",2}}),"Material fixture failed");
        GameSproutRuntime::RenderView view;view.Scene=scene;view.DrawGrid=false;view.ClearColor={0,0,0,1};view.Camera.Position={0,0,3};
        Check(GameSproutRuntime::TryMakeLookAt(view.Camera.Position,{0,0,0},{0,1,0},view.Camera.View),"Cannot create explicit camera");
        runtime->Update(0);Probe probe;Check(runtime->RegisterListener(&probe),"Cannot observe derived bounds changes");
        Check(renderer->Render(view),renderer->GetLastError());
        auto pixel=Center(*renderer);
        Check(pixel[1]>120 && pixel[0]<5 && pixel[2]<5,"Actual HLSL matrix/texture pixel is not lit green");
        Check(renderer->GetStatistics().VisibleObjects==1 && renderer->GetStatistics().Triangles==1 && mesh->GetLocalBounds().Extents.X==1,
              "Renderer mesh/statistics/loaded bounds mismatch");
        runtime->Update(0);Check(probe.Calls==0 && scene->GetChildCount()==1 && !scene->GetMainCamera(),"Render changed scene/main camera or emitted derived-cache changes");
        GameSproutRuntime::RenderedCamera rendered;
        Check(renderer->GetRenderedCamera(rendered)&&rendered.SceneID==scene->GetInstanceID(),"Scene frame has no camera snapshot");
        auto clear=view;clear.DrawScene=false;clear.Scene=nullptr;clear.DrawGrid=true;clear.ClearColor={.25f,.5f,.75f,1};
        clear.Camera.View.Data[0]=std::numeric_limits<float>::quiet_NaN();
        GameSproutRuntime::DebugDrawList ignoredDebug;
        ignoredDebug.Lines.push_back({{std::numeric_limits<float>::quiet_NaN(),0,0},{}, {}});clear.DebugDraw=&ignoredDebug;
        Check(renderer->Render(clear),renderer->GetLastError());pixel=Center(*renderer);
        Check(std::abs(pixel[0]-64)<=1&&std::abs(pixel[1]-128)<=1&&std::abs(pixel[2]-191)<=1,"Clear-only view retained previous geometry or ignored clear color");
        Check(!renderer->GetRenderedCamera(rendered)&&rendered.SceneID==0&&rendered.Width==0&&
            renderer->GetStatistics().DrawCalls==0&&renderer->GetStatistics().Triangles==0,"Clear-only view retained a pick camera or submitted grid/debug/scene geometry");
        GameSproutRuntime::SceneRaycastHit clearHit;clearHit.ObjectID=123;
        Check(!renderer->Raycast(nullptr,{{0,0,3},{0,0,-1}},clearHit)&&clearHit.ObjectID==0,"A missing scene reused stale picking data");
        Check(renderer->Raycast(scene,{{0,0,3},{0,0,-1}},clearHit)&&clearHit.ObjectID==object->GetInstanceID(),"Explicit scene queries incorrectly depended on the previous view camera");
        Check(renderer->Render(view)&&renderer->GetRenderedCamera(rendered),"Scene rendering did not recover after clear-only frame");
        const int loads=provider.MeshLoads;
        Check(renderer->Render(view) && provider.MeshLoads==loads,"Unchanged prepared mesh was uploaded again");
        for(const auto flags:std::initializer_list<std::array<bool,2>>{{true,true},{false,true},{true,false},{false,false}}) {
            provider.UV=flags[0];provider.Color=flags[1];++provider.Revision;
            Check(renderer->Render(view),renderer->GetLastError());pixel=Center(*renderer);
            Check(flags[0]?(pixel[1]>120 && pixel[0]<5):(pixel[0]>120 && pixel[1]>120 && pixel[2]>120),
                  "Missing UV/color shader variant read wrong vertex fields");
        }
        provider.UV=true;provider.Color=true;provider.Green=128;provider.SRGB=true;++provider.Revision;
        Check(renderer->Render(view),renderer->GetLastError());const auto srgb=Center(*renderer);
        provider.SRGB=false;++provider.Revision;Check(renderer->Render(view),renderer->GetLastError());const auto linear=Center(*renderer);
        Check(srgb[1]>20 && linear[1]>srgb[1]+25,"sRGB source texture decoding was not applied");
        provider.Green=255;provider.SRGB=true;++provider.Revision;
        auto* front=scene->CreateGameObject("Near red triangle");front->GetTransform()->SetLocalPosition({0,0,0.5f});
        front->AddComponent<GameSproutRuntime::IMeshComponent>()->SetMesh(mesh->GetMesh());
        front->AddComponent<GameSproutRuntime::IMeshRendererComponent>()->SetMaterials({{"0123456789abcdef0123456789abcdef",4}});
        // Render near object first and far green object second: depth must keep the red foreground.
        auto* reorder=scene->CreateGameObject("Reorder helper");
        Check(reorder->AddChild(object,false) && scene->AddChild(object,false) && scene->DestroyGameObject(reorder),"Cannot reorder depth fixture");
        Check(renderer->Render(view),renderer->GetLastError());pixel=Center(*renderer);
        Check(pixel[0]>120 && pixel[1]<5,"Depth test allowed farther geometry to overwrite foreground");
        Check(scene->DestroyGameObject(front),"Cannot remove depth fixture");
        meshRenderer->SetMaterials({});provider.DiagonalNormal=true;++provider.Revision;
        object->GetTransform()->SetLocalScale({1,0.2f,1});
        Check(renderer->Render(view),renderer->GetLastError());pixel=Center(*renderer);
        Check(pixel[0]>190,"Nonuniform scale did not use inverse-transpose normals");
        object->GetTransform()->SetLocalScale({-1,1,1});
        Check(renderer->Render(view),renderer->GetLastError());pixel=Center(*renderer);
        Check(pixel[0]>20,"Negative scale winding was culled incorrectly");
        object->GetTransform()->SetLocalScale({0,1,1});Check(renderer->Render(view),renderer->GetLastError());
        Check(Center(*renderer)[0]==0 && renderer->GetStatistics().SkippedObjects==1,"Singular transform was not safely skipped");
        object->GetTransform()->SetLocalScale({1,1,1});object->SetActive(false);
        Check(renderer->Render(view) && Center(*renderer)[0]==0,"Inactive object was rendered");object->SetActive(true);
        meshRenderer->SetVisible(false);Check(renderer->Render(view) && Center(*renderer)[0]==0,"Hidden renderer was rendered");meshRenderer->SetVisible(true);
        Check(renderer->Render(view),renderer->GetLastError());
        CheckSceneTools(*runtime,*renderer,*scene,*object,provider,view);
        provider.BadIndex=true;++provider.Revision;Check(renderer->Render(view),renderer->GetLastError());
        Check(renderer->GetStatistics().DrawCalls==0 && !renderer->GetLastError().empty(),"Corrupt prepared indices were sent to GPU or had no diagnostic");
        provider.BadIndex=false;provider.Missing=true;++provider.Revision;Check(renderer->Render(view),renderer->GetLastError());
        Check(Center(*renderer)[0]==0 && renderer->GetStatistics().VisibleObjects==0 && !renderer->GetLastError().empty() &&
              mesh->GetLocalBounds().Extents.X==0,"Missing mesh was replaced with fake geometry or retained stale derived bounds");
        auto hiddenView=view;Check(renderer->Resize(0,0,error),error);
        Check(renderer->Render(hiddenView) && !renderer->GetStatistics().Width && !renderer->GetStatistics().Height,"Unavailable Surface was not a successful skip");
        GameSproutRuntime::RenderedCamera unavailableCamera;
        Check(!renderer->GetRenderedCamera(unavailableCamera) && !unavailableCamera.Width,"Unavailable view retained a stale pixel camera");
        Check(renderer->Resize(128,128,error),error);
        bool rejected=false;scene->ForeachChild([&](GameSproutRuntime::IGameObject*) {rejected=!renderer->Render(view);});
        Check(rejected,"Renderer ran from Runtime traversal callback");
        auto* empty=runtime->CreateScene("Grid only");view.Scene=empty;view.DrawGrid=true;view.Camera.Position={0,3,5};
        Check(GameSproutRuntime::TryMakeLookAt(view.Camera.Position,{0,0,0},{0,1,0},view.Camera.View),"Grid camera failed");
        Check(renderer->Render(view),renderer->GetLastError());
        std::vector<std::uint8_t> pixels;Check(renderer->ReadPixels(pixels,error),error);
        std::size_t colored=0;for(std::size_t i=0;i<pixels.size();i+=4) if(pixels[i]||pixels[i+1]||pixels[i+2])++colored;
        Check(colored>20 && renderer->GetStatistics().DrawCalls==1,"Empty scene reference grid missing");
        Check(renderer->Resize(64,96,error),error);Check(renderer->Render(view) && renderer->GetStatistics().Width==64 && renderer->GetStatistics().Height==96,"Resized viewport did not rebuild depth/projection");
        Check(!renderer->Resize(0xffffffffu,0xffffffffu,error),"Oversized viewport accepted");
        Check(renderer->Render(view)&&renderer->GetStatistics().Width==64,"Failed viewport resize destroyed its previous target");
        Check(renderer->Resize(128,128,error),error);Check(renderer->Render(view),renderer->GetLastError());
        runtime->UnregisterListener(&probe);
        delete renderer;renderer=nullptr;delete runtime;runtime=nullptr;
        std::cout<<"PASS actual GPU Runtime Renderer: HLSL matrices, four vertex variants, texture/sRGB, depth, inverse-transpose normals, negative/zero scale, grid/resize, revision cache and missing/corrupt assets\n";
        return 0;
    } catch(const std::exception& exception) {
        delete renderer;delete runtime;
        std::cerr<<"FAIL "<<exception.what()<<'\n';return 1;
    }
}
