#include "GameSproutRuntimeExport.h"
#include "RuntimeRenderApi.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
void Float(std::vector<std::uint8_t>& bytes,float value){const auto* source=reinterpret_cast<const std::uint8_t*>(&value);bytes.insert(bytes.end(),source,source+4);}
const GameSproutRuntime::ResourceReference MeshReference{"1349119f706a432ea716f963a18c4001",1};
class Provider final:public GameSproutRuntime::IRuntimeResourceProvider {
public:
    std::uint64_t Revision=1;
    unsigned MeshLoads=0;
    bool Missing=false;
    std::uint64_t GetRevision()const override{return Revision;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MeshRenderData& data,std::string& error)override{
        ++MeshLoads;data={};if(Missing){error="Missing debug mesh";return false;}
        // Tetrahedron plus an interior triangle. Convex cooking must remove
        // interior edges, rather than draw the source triangle mesh or its AABB.
        const float positions[][3]={{-.6f,-.5f,-.5f},{.6f,-.5f,-.5f},{0,-.5f,.6f},{0,.7f,0},{0,-.1f,0}};
        data.VertexCount=5;data.VertexStride=24;data.IndexCount=15;
        for(const auto& position:positions){for(float value:position)Float(data.Bytes,value);Float(data.Bytes,0);Float(data.Bytes,1);Float(data.Bytes,0);}
        data.IndexOffset=data.Bytes.size();
        for(std::uint32_t index:{0u,2u,1u,0u,1u,3u,1u,2u,3u,2u,0u,3u,0u,4u,1u})
            for(unsigned byte=0;byte<4;++byte)data.Bytes.push_back(std::uint8_t(index>>(byte*8)));
        data.LocalBounds={{0,.1f,.05f},{.6f,.6f,.55f}};return true;
    }
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData&,std::string&)override{return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData&,std::string&)override{return false;}
};
void Capture(GameSproutRuntime::IRuntimeViewport& renderer,const char* path) {
    std::vector<std::uint8_t> pixels;std::string error;Check(renderer.ReadPixels(pixels,error),error);
    const auto& stats=renderer.GetStatistics();std::ofstream stream(path,std::ios::binary);
    stream<<"P6\n"<<stats.Width<<' '<<stats.Height<<"\n255\n";
    for(std::size_t index=0;index<pixels.size();index+=4)stream.write(reinterpret_cast<const char*>(pixels.data()+index),3);
    Check(stream.good(),"Cannot save collider GPU capture");
}
bool Near(float a,float b){return std::fabs(a-b)<.0001f;}
}
int main(int argc,char** argv) {
    using namespace GameSproutRuntime;
    IGameSproutRuntime* runtime=nullptr;IRuntimeViewport* renderer=nullptr;Provider provider;std::string error;
    try {
        Check(argc==2,"Pass SDK binary directory");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        renderer=runtime->CreateOffscreenRenderer(1200,600,&provider,error);
        if(!renderer){delete runtime;std::cout<<"SKIP collider GPU unavailable: "<<error<<'\n';return 77;}
        auto* scene=runtime->CreateScene("Collider debug scene");
        auto* boxObject=scene->CreateGameObject("Box");boxObject->GetTransform()->SetLocalPosition({-4,0,0});
        auto* box=boxObject->AddComponent<IBoxColliderComponent>();Check(box&&box->SetShape({{1,1.5f,1}}),"Box fixture");
        auto* sphereObject=scene->CreateGameObject("Sphere");sphereObject->GetTransform()->SetLocalPosition({-2,0,0});
        auto* sphere=sphereObject->AddComponent<ISphereColliderComponent>();Check(sphere&&sphere->SetShape({.7f}),"Sphere fixture");
        auto* capsuleObject=scene->CreateGameObject("Capsule trigger");
        auto* capsule=capsuleObject->AddComponent<ICapsuleColliderComponent>();Check(capsule&&capsule->SetShape({.4f,2,PhysicsAxis::Y}),"Capsule fixture");
        auto settings=capsule->GetColliderSettings();settings.IsTrigger=true;Check(capsule->SetColliderSettings(settings),"Trigger fixture");
        auto* triangleObject=scene->CreateGameObject("Triangle mesh");triangleObject->GetTransform()->SetLocalPosition({2,0,0});
        auto* triangle=triangleObject->AddComponent<IMeshColliderComponent>();Check(triangle&&triangle->SetShape({MeshReference,false}),"Triangle collider fixture");
        auto* convexObject=scene->CreateGameObject("Convex hull");convexObject->GetTransform()->SetLocalPosition({4,0,0});
        auto* convex=convexObject->AddComponent<IMeshColliderComponent>();Check(convex&&convex->SetShape({MeshReference,true}),"Convex collider fixture");
        ColliderDebugOptions options;DebugDrawList draw;
        Check(renderer->AppendColliderWireframes(scene,options,draw)&&draw.Lines.empty(),"Unselected default drew every collider");
        options.SelectedObjectID=boxObject->GetInstanceID();
        Check(renderer->AppendColliderWireframes(scene,options,draw)&&draw.Lines.size()==12,"Selected Box must have twelve exact edges without a MeshRenderer");
        for(const auto& line:draw.Lines)Check(Near(std::fabs(line.From.X+4),.5f)&&Near(std::fabs(line.From.Y),.75f)&&Near(std::fabs(line.From.Z),.5f),"Box wire does not match its shape");
        auto* child=scene->CreateGameObject("Child collider",boxObject);child->GetTransform()->SetLocalPosition({0,2,0});child->AddComponent<IBoxColliderComponent>();
        draw={};Check(renderer->AppendColliderWireframes(scene,options,draw)&&draw.Lines.size()==24,"Selecting parent did not include child Collider");
        options.IncludeChildren=false;draw={};Check(renderer->AppendColliderWireframes(scene,options,draw)&&draw.Lines.size()==12,"IncludeChildren=false included child collider");
        options.IncludeChildren=true;
        options.SelectedObjectID=triangleObject->GetInstanceID();draw={};Check(renderer->AppendColliderWireframes(scene,options,draw),renderer->GetLastError());
        const auto triangleEdges=draw.Lines.size();Check(triangleEdges==8,"Nonconvex wireframe is not its eight unique triangle edges");
        options.SelectedObjectID=convexObject->GetInstanceID();draw={};Check(renderer->AppendColliderWireframes(scene,options,draw),renderer->GetLastError());
        Check(draw.Lines.size()==6&&draw.Lines.size()<triangleEdges,"Convex wireframe did not use the cooked hull");
        const auto loads=provider.MeshLoads;
        for(unsigned pass=0;pass<3;++pass){draw={};Check(renderer->AppendColliderWireframes(scene,options,draw),renderer->GetLastError());}
        Check(provider.MeshLoads==loads,"Repeated debug draw reloaded unchanged mesh");
        convexObject->GetTransform()->SetLocalScale({-2,1,.5f});
        draw={};Check(renderer->AppendColliderWireframes(scene,options,draw)&&draw.Lines.size()==6,"Mirrored nonuniform convex debug transform failed");
        for(const auto& line:draw.Lines)Check(line.From.X>=2.79f&&line.From.X<=5.21f&&line.From.Z>=-.26f&&line.From.Z<=.31f,"Cooked hull world transform is wrong");
        convexObject->GetTransform()->SetLocalScale({1,1,1});
        options.SelectedObjectID=sphereObject->GetInstanceID();draw={};Check(renderer->AppendColliderWireframes(scene,options,draw)&&draw.Lines.size()==96,"Sphere must use three great circles");
        const auto selectedTint=draw.Lines.front().Tint;
        sphereObject->GetTransform()->SetLocalScale({-2,3,4});draw={};
        Check(renderer->AppendColliderWireframes(scene,options,draw)&&draw.Lines.size()==96,"Nonuniform sphere wireframe was rejected");
        for(const auto& line:draw.Lines) {
            const auto delta=line.From-Vector3{-2,0,0};Check(Near(delta.Length(),2.8f),"Sphere wireframe differs from physical max-axis radius");
        }
        sphereObject->GetTransform()->SetLocalScale({1,1,1});
        capsuleObject->GetTransform()->SetLocalScale({-2,3,4});options.SelectedObjectID=capsuleObject->GetInstanceID();draw={};
        Check(renderer->AppendColliderWireframes(scene,options,draw)&&draw.Lines.size()==132,"Nonuniform capsule wireframe was rejected");
        float capsuleTop=-100,capsuleSide=0;
        // The hemisphere pole is the final arc segment's To endpoint. Bounds
        // must consider both endpoints, as the renderer and physical surface do.
        for(const auto& line:draw.Lines)for(const auto point:{line.From,line.To}) {
            capsuleTop=std::max(capsuleTop,point.Y);capsuleSide=std::max(capsuleSide,std::fabs(point.X));
        }
        Check(Near(capsuleTop,3)&&Near(capsuleSide,1.6f),"Capsule wireframe differs from its physical scaled radius/height");
        capsuleObject->GetTransform()->SetLocalScale({1,1,1});options.SelectedObjectID=sphereObject->GetInstanceID();
        settings=sphere->GetColliderSettings();settings.Enabled=false;Check(sphere->SetColliderSettings(settings),"Disable sphere");
        draw={};Check(renderer->AppendColliderWireframes(scene,options,draw)&&draw.Lines.size()==96&&draw.Lines.front().Tint.R<selectedTint.R+.01f&&draw.Lines.front().Tint.G<.8f,"Disabled selected collider must remain visible in grey");
        options.SelectedObjectID=0;options.DrawAll=true;draw={};Check(renderer->AppendColliderWireframes(scene,options,draw)&&draw.Lines.size()==12+12+132+8+6,"DrawAll did not filter disabled collider or include non-rendered shapes");
        settings.Enabled=true;Check(sphere->SetColliderSettings(settings),"Enable sphere");
        // Apply Center and local rotation to physical geometry, not mesh bounds.
        settings=box->GetColliderSettings();settings.Center={.25f,.5f,-.25f};settings.Rotation={0,0,.70710678f,.70710678f};Check(box->SetColliderSettings(settings),"Collider center/rotation");
        options.DrawAll=false;options.SelectedObjectID=boxObject->GetInstanceID();options.IncludeChildren=false;draw={};
        Check(renderer->AppendColliderWireframes(scene,options,draw)&&draw.Lines.size()==12,"Rotated Box debug failed");
        for(const auto& line:draw.Lines)Check(Near(std::fabs(line.From.X+3.75f),.75f)&&Near(std::fabs(line.From.Y-.5f),.5f)&&Near(std::fabs(line.From.Z+.25f),.5f),"Collider center/rotation was not used");
        settings.Center={};settings.Rotation={};Check(box->SetColliderSettings(settings),"Reset collider pose");
        options.SelectedObjectID=UINT64_MAX;const auto old=draw.Lines.size();Check(!renderer->AppendColliderWireframes(scene,options,draw)&&draw.Lines.size()==old,"Invalid query partially changed draw list");
        options.SelectedObjectID=convexObject->GetInstanceID();provider.Missing=true;++provider.Revision;
        Check(renderer->AppendColliderWireframes(scene,options,draw)&&draw.Lines.size()==old&&!renderer->GetLastError().empty(),"Deleted resource retained stale cooked hull or had no diagnostic");
        options.DrawAll=true;draw={};
        Check(renderer->AppendColliderWireframes(scene,options,draw)&&draw.Lines.size()==12+12+96+132,
            "A missing mesh suppressed valid collider wireframes in DrawAll");
        boxObject->GetTransform()->SetLocalScale({0,1,1});draw={};
        Check(renderer->AppendColliderWireframes(scene,options,draw)&&draw.Lines.size()==96+132,
            "A singular shape suppressed valid collider wireframes in DrawAll");
        boxObject->GetTransform()->SetLocalScale({1,1,1});
        provider.Missing=false;++provider.Revision;
        options.DrawAll=true;options.IncludeChildren=true;options.SelectedObjectID=sphereObject->GetInstanceID();draw={};
        Check(renderer->AppendColliderWireframes(scene,options,draw),renderer->GetLastError());
        RenderView view;view.Scene=scene;view.DrawGrid=false;view.DebugDraw=&draw;view.ClearColor={.1f,.12f,.15f,1};view.Camera.Position={6,5,11};
        Check(TryMakeLookAt(view.Camera.Position,{0,0,0},{0,1,0},view.Camera.View),"Camera setup");
        Check(renderer->Render(view),renderer->GetLastError());
        std::vector<std::uint8_t> pixels;Check(renderer->ReadPixels(pixels,error),error);
        std::size_t green=0,cyan=0;
        for(std::size_t i=0;i<pixels.size();i+=4){green+=pixels[i+1]>150&&pixels[i+1]>pixels[i]+30&&pixels[i+1]>pixels[i+2]+30;cyan+=pixels[i+1]>150&&pixels[i+2]>150&&pixels[i]<100;}
        Check(green>300&&cyan>100,"Collider wireframes were generated but not rendered into GPU pixels");
        Capture(*renderer,"/tmp/gamesprout-collider-wireframes.ppm");
        options.DrawAll=false;draw={};Check(renderer->AppendColliderWireframes(scene,options,draw),renderer->GetLastError());
        Check(renderer->Render(view),renderer->GetLastError());Capture(*renderer,"/tmp/gamesprout-collider-selected.ppm");
        Check(!runtime->IsScriptExecutionEnabled(),"Debug view started gameplay/physics simulation");
        delete renderer;renderer=nullptr;delete runtime;runtime=nullptr;
        std::cout<<"PASS collider wireframes: selected hierarchy/all filtering, exact primitive/mesh/cooked hull, resource revision, GPU pixels\n";return 0;
    }catch(const std::exception& failure){delete renderer;delete runtime;std::cerr<<"FAIL "<<failure.what()<<'\n';return 1;}
}
