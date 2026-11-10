#include "GameSproutRuntimeExport.h"
#include "RuntimeRenderApi.h"
#include "RuntimeUIApi.h"
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
        data.Pixels={0,Green,0,255,0,Green,0,255,0,Green,0,255,0,Green,0,255};return true;
    }
    bool LoadMaterial(const GameSproutRuntime::ResourceReference& reference,GameSproutRuntime::MaterialRenderData& data,std::string& error) override {
        ++MaterialLoads;data={};
        if(MissingMaterial) {error="Missing prepared material fixture";return false;}
        if(reference.LocalID==2) data.BaseColorTexture={"0123456789abcdef0123456789abcdef",3};
        if(reference.LocalID==4) data.BaseColor={1,0,0,1};
        return true;
    }
    bool LoadFont(const GameSproutRuntime::ResourceReference& reference,GameSproutRuntime::FontRenderData& data,std::string& error) override {
        if(reference.LocalID!=6)return IRuntimeResourceProvider::LoadFont(reference,data,error);
        data={};
        data.Atlas={reference.Guid,7};data.PixelSize=2;data.Ascent=2;
        data.Glyphs={{0x6C49,0,0,2,2,0,-2,2}};
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
static void CheckUI(GameSproutRuntime::IGameSproutRuntime& runtime,GameSproutRuntime::IRuntimeViewport& renderer) {
    using namespace GameSproutRuntime;
    auto* scene=runtime.CreateScene("Screen UI");Check(scene,"Cannot create UI scene");
    auto* canvas=scene->CreateGameObject("Canvas");
    auto* canvasComponent=canvas->AddComponent<ICanvasComponent>();
    Check(canvasComponent&&canvasComponent->SetReferenceResolution({128,128}),"Cannot configure Canvas");
    Check(canvas->AddComponent<IRectTransformComponent>(),"Canvas rectangle unavailable");
    auto* panel=scene->CreateGameObject("Panel",canvas);
    panel->AddComponent<IRectTransformComponent>()->SetSizeDelta({80,60});
    auto* panelImage=panel->AddComponent<IImageComponent>();panelImage->SetColor({1,0,0,0.5f});
    auto* sprite=scene->CreateGameObject("Top-left Image",canvas);
    auto* spriteRect=sprite->AddComponent<IRectTransformComponent>();
    spriteRect->SetAnchors({0,1},{0,1});spriteRect->SetPivot({0,1});
    spriteRect->SetAnchoredPosition({8,-8});spriteRect->SetSizeDelta({16,16});
    const std::string guid="0123456789abcdef0123456789abcdef";
    sprite->AddComponent<IImageComponent>()->SetTexture({guid,3});
    auto* label=scene->CreateGameObject("Text",panel);
    label->AddComponent<IRectTransformComponent>()->SetSizeDelta({75,50});
    auto* text=label->AddComponent<ITextComponent>();
    Check(text&&text->SetFontSize(18)&&text->SetText("UI"),"Cannot configure text fixture");

    RenderView view;view.Scene=scene;view.DrawScene=false;view.DrawUI=true;view.ClearColor={0,0,0,1};
    Check(renderer.Render(view),renderer.GetLastError());
    RenderedCamera camera;
    Check(!renderer.GetRenderedCamera(camera)&&renderer.GetStatistics().UIDrawCalls==3,"UI-only view requires a camera or lost draws");
    std::vector<std::uint8_t> pixels;std::string error;
    Check(renderer.ReadPixels(pixels,error),error);
    const auto pixel=[&](unsigned x,unsigned y,unsigned channel) {return pixels[(y*128u+x)*4u+channel];};
    Check(pixel(35,47,0)>=127&&pixel(35,47,0)<=129&&pixel(35,47,1)==0,"UI panel did not alpha blend over the clear color");
    Check(pixel(12,12,1)==255&&pixel(12,115,1)==0,"UI texture not drawn at the top-left layout rectangle");
    unsigned white=0;
    for(unsigned y=45;y<83;++y)for(unsigned x=40;x<88;++x)
        if(pixel(x,y,0)>200&&pixel(x,y,1)>200&&pixel(x,y,2)>200)++white;
    Check(white>20,"Fallback font did not render visible glyphs");

    text->SetFont({guid,6});
    Check(text->SetText("\xE6\xB1\x89"),"Cannot set UTF-8 text");
    Check(renderer.Render(view),renderer.GetLastError());
    Check(renderer.ReadPixels(pixels,error),error);
    Check(pixel(64,64,0)==0&&pixel(64,64,1)==255,"Prepared UTF-8 font glyph did not sample its atlas");
    text->SetText("");
    Check(renderer.Render(view)&&renderer.ReadPixels(pixels,error),renderer.GetLastError()+error);
    Check(pixel(64,64,0)>=127&&pixel(64,64,1)==0,"Clearing Text retained cached geometry");

    view.DrawScene=true;view.DrawGrid=false;
    Check(renderer.Render(view),renderer.GetLastError());
    Check(renderer.GetStatistics().UIDrawCalls==2&&renderer.GetRenderedCamera(camera),"UI overlay failed with a 3D depth target");
    view.DrawScene=false;
    view.UIPreviewObjectID=canvas->GetInstanceID();
    Check(renderer.Render(view)&&renderer.ReadPixels(pixels,error),renderer.GetLastError()+error);
    Check(pixel(5,64,2)==0&&pixel(11,64,2)>200,"Canvas preview did not draw a bounded 2D selection frame");
    Check(runtime.PickUIScreenPreview(scene,view.UIPreviewObjectID,{128,128},{22,22})==sprite->GetInstanceID(),
        "Canvas preview hit test did not use its framed layout");
    Check(!runtime.PickUIScreenPreview(scene,view.UIPreviewObjectID,{128,128},{5,64}),
        "Canvas preview intercepted a click outside its frame");
    canvasComponent->SetRenderMode(CanvasRenderMode::WorldSpace);
    Check(renderer.Render(view)&&renderer.GetStatistics().UIDrawCalls==0,"World-space Canvas entered the screen-space preview");
    canvasComponent->SetRenderMode(CanvasRenderMode::ScreenSpace);
    view.UIPreviewObjectID=0;

    Check(renderer.Render(view)&&renderer.ReadPixels(pixels,error),renderer.GetLastError()+error);
    Check(pixel(12,12,1)==255,"UI fixture lost its original placement before rotation");
    Check(sprite->GetTransform()->SetLocalRotation({0,0,0.70710678f,0.70710678f}),"Cannot rotate sprite about its upper-left pivot");
    Check(renderer.Render(view)&&renderer.ReadPixels(pixels,error),renderer.GetLastError()+error);
    Check(pixel(12,4,1)==255&&pixel(12,12,1)==0,"Game UI did not update its clipped and rotated sprite");

    view.UIPreviewObjectID=sprite->GetInstanceID();
    view.UIScreenPreviewLayoutSize={128,64};
    Check(renderer.Render(view)&&renderer.ReadPixels(pixels,error),renderer.GetLastError()+error);
    Check(pixel(15,39,1)==255&&pixel(15,35,1)==0,"Rotated Scene UI was not clipped by its canvas frame");
    Check(runtime.PickUIScreenPreview(scene,view.UIPreviewObjectID,{128,128},{15.5f,39.5f},{128,64})==sprite->GetInstanceID()&&
        !runtime.PickUIScreenPreview(scene,view.UIPreviewObjectID,{128,128},{15,35},{128,64}),
        "Scene preview rendering and picking disagreed at the canvas clip");

    UIDebugDrawList overlay;
    overlay.Triangles.push_back({{13,38},{20,38},{13,44},{1,0,1,1}});
    view.UIScreenDebugDraw=&overlay;
    Check(renderer.Render(view)&&renderer.ReadPixels(pixels,error),renderer.GetLastError()+error);
    Check(pixel(15,39,0)==255&&pixel(15,39,1)==0&&pixel(15,39,2)==255,"Host screen overlay did not draw over Scene UI");
    view.UIScreenDebugDraw=nullptr;
    view.UIPreviewObjectID=0;
    view.DrawScene=true;
    view.DrawUI=false;
    Check(renderer.Render(view)&&renderer.GetStatistics().UIDrawCalls==0,"UI was drawn when disabled on this view");
}
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
        Check(object->SetLayer(31),"Cannot set high-bit render layer");
        view.Camera.Projection.CullingMask=1u<<31;
        Check(renderer->Render(view),renderer->GetLastError());pixel=Center(*renderer);
        Check(pixel[1]>120 && renderer->GetStatistics().VisibleObjects==1,"Layer31 object was rejected by matching camera mask");
        view.Camera.Projection.CullingMask=1u;
        Check(renderer->Render(view),renderer->GetLastError());pixel=Center(*renderer);
        Check(pixel[0]<5&&pixel[1]<5&&pixel[2]<5&&renderer->GetStatistics().VisibleObjects==0,
              "Camera mask ignored the object's authored Layer");
        Check(object->SetLayer(0),"Cannot restore render fixture layer");
        view.Camera.Projection.CullingMask=GameSproutRuntime::AllLayers;
        Check(renderer->Render(view),renderer->GetLastError());
        runtime->Update(0);probe.Calls=0;
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
        CheckUI(*runtime,*renderer);
        runtime->UnregisterListener(&probe);
        delete renderer;renderer=nullptr;delete runtime;runtime=nullptr;
        std::cout<<"PASS actual GPU Runtime Renderer: mesh and depth, viewport, resource cache, screen UI blending, texture placement and font glyphs\n";
        return 0;
    } catch(const std::exception& exception) {
        delete renderer;delete runtime;
        std::cerr<<"FAIL "<<exception.what()<<'\n';return 1;
    }
}
