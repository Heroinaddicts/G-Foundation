#include "GameSproutRuntimeExport.h"
#include "RuntimeRenderApi.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace GameSproutRuntime;
namespace {
void Check(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
class Provider final:public IRuntimeResourceProvider {
public:
    std::uint64_t Revision=1;unsigned MeshLoads=0,ShaderLoads=0;
    bool BadBounds=false,CustomShader=false,Skin=false;float VertexOffset=0;
    std::uint64_t GetRevision() const override{return Revision;}
    bool LoadMesh(const ResourceReference&,MeshRenderData& data,std::string&) override {
        ++MeshLoads;data={};data.VertexCount=3;data.VertexStride=Skin?56:24;data.IndexCount=3;data.IndexOffset=std::size_t(data.VertexStride)*3;
        if(Skin){data.BoneIndicesOffset=24;data.BoneWeightsOffset=40;data.Bones={{"Bone",{}}};}
        data.Bytes.resize(data.IndexOffset+12,0);const float vertices[3][6]={{-.2f,-.2f,0,0,0,1},{.2f,-.2f,0,0,0,1},{0,.2f,0,0,0,1}};
        for(unsigned i=0;i<3;++i){auto* destination=data.Bytes.data()+std::size_t(i)*data.VertexStride;std::memcpy(destination,vertices[i],24);
            float x=vertices[i][0]+VertexOffset;std::memcpy(destination,&x,4);if(Skin){const float weight=1;std::memcpy(destination+40,&weight,4);}}
        const std::uint32_t indices[]={0,1,2};std::memcpy(data.Bytes.data()+data.IndexOffset,indices,12);
        data.LocalBounds=BadBounds?Bounds{{200,0,0},{0,0,0}}:Bounds{{VertexOffset,0,0},{.2f,.2f,0}};return true;
    }
    bool LoadTexture(const ResourceReference&,TextureRenderData&,std::string&) override{return false;}
    bool LoadMaterial(const ResourceReference&,MaterialRenderData& data,std::string&) override {data={};if(CustomShader)data.Shader={"0123456789abcdef0123456789abcdef",3};return true;}
    bool LoadShader(const ResourceReference&,ShaderRenderData&,std::string& error) override{++ShaderLoads;error="Custom displacement Shader request reached provider";return false;}
};
int Center(IRuntimeViewport& viewport){std::vector<std::uint8_t> pixels;std::string error;Check(viewport.ReadPixels(pixels,error),error);
    const auto& stats=viewport.GetStatistics();return pixels[(std::size_t(stats.Height/2)*stats.Width+stats.Width/2)*4];}
}
int main(int argc,char** argv){
    IGameSproutRuntime* runtime=nullptr;IRuntimeViewport* viewport=nullptr;Provider provider;std::string error;
    try{
        Check(argc==2,"Pass Runtime binary directory");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        viewport=runtime->CreateOffscreenRenderer(128,128,&provider,error);
        if(!viewport){delete runtime;std::cout<<"SKIP GPU unavailable: "<<error<<'\n';return 77;}
        auto* scene=runtime->CreateScene("Conservative render culling");auto* object=scene->CreateGameObject("Triangle");
        auto* mesh=object->AddComponent<IMeshComponent>();mesh->SetMesh({"0123456789abcdef0123456789abcdef",1});
        object->AddComponent<IMeshRendererComponent>()->SetMaterials({{"0123456789abcdef0123456789abcdef",2}});
        RenderView view;view.Scene=scene;view.DrawGrid=false;view.ClearColor={0,0,0,1};view.Camera.Position={0,0,3};
        view.Camera.Projection.Projection=CameraProjection::Orthographic;view.Camera.Projection.OrthographicHeight=2;
        view.Camera.Projection.NearPlane=.1f;view.Camera.Projection.FarPlane=10;
        Check(TryMakeLookAt(view.Camera.Position,{0,0,0},{0,1,0},view.Camera.View),"Cannot aim camera");
        const auto render=[&]{Check(viewport->Render(view),viewport->GetLastError());return viewport->GetStatistics().DrawCalls;};
        Check(render()==1&&Center(*viewport)>100,"A centered static mesh was culled");
        provider.BadBounds=true;++provider.Revision;Check(render()==1&&Center(*viewport)>100,"Unreliable source bounds culled validated visible vertices");
        Check(mesh->GetLocalBounds().Center.X==200,"Private culling bounds replaced the provider's LocalBounds contract");
        provider.BadBounds=false;++provider.Revision;object->GetTransform()->SetLocalPosition({100,0,0});
        Check(render()==0&&viewport->GetStatistics().SkippedObjects==1,"Outside static mesh still produced a draw");
        Bounds bounds;Check(viewport->GetWorldBounds(scene,object->GetInstanceID(),bounds)&&bounds.Center.X==100,"Culling disabled editor bounds queries");
        SceneRaycastHit hit;Check(viewport->Raycast(scene,{{100,0,3},{0,0,-1}},hit)&&hit.ObjectID==object->GetInstanceID(),"Culling disabled explicit offscreen editor picking");
        DebugDrawList outline;Check(viewport->AppendSelectionOutline(scene,object->GetInstanceID(),outline)&&outline.Lines.size()==3,"Culling disabled offscreen selection outlines");
        auto secondView=view;secondView.Camera.Position={100,0,3};Check(TryMakeLookAt(secondView.Camera.Position,{100,0,0},{0,1,0},secondView.Camera.View),"Second camera failed");
        Check(viewport->Render(secondView)&&viewport->GetStatistics().DrawCalls==1&&Center(*viewport)>100,"One viewport reused another viewport's frustum");
        Check(render()==0,"Returning to the original camera retained stale culling state");
        object->GetTransform()->SetLocalPosition({1.5f,0,0});Check(render()==0,"Narrow viewport retained the outside box");
        Check(viewport->Resize(256,128,error),error);Check(render()==1,"Actual resized target aspect did not update frustum");
        Check(viewport->Resize(128,128,error),error);object->GetTransform()->SetLocalPosition({0,0,2.95f});Check(render()==0,"Near plane was not applied before draw submission");
        object->GetTransform()->SetLocalPosition({0,0,2.9f});Check(render()==1,"Near-plane tangent mesh was culled");
        object->GetTransform()->SetLocalPosition({0,0,-8});Check(render()==0,"Far plane was not applied before draw submission");
        object->GetTransform()->SetLocalPosition({0,0,-7});Check(render()==1,"Far-plane tangent mesh was culled");
        object->GetTransform()->SetLocalPosition({0,0,4});Check(render()==0,"Behind-camera mesh was retained");
        object->GetTransform()->SetLocalPosition({0,0,0});
        auto* bulk=scene->CreateGameObject("Outside instances");
        for(unsigned i=0;i<500;++i){auto* node=scene->CreateGameObject("Outside",bulk);node->GetTransform()->SetLocalPosition({100+float(i),0,0});
            node->AddComponent<IMeshComponent>()->SetMesh(mesh->GetMesh());node->AddComponent<IMeshRendererComponent>();}
        const auto loads=provider.MeshLoads;Check(render()==1&&viewport->GetStatistics().SkippedObjects==500,"Large offscreen set still generated draw records");
        Check(render()==1&&provider.MeshLoads==loads,"Unchanged shared geometry was rescanned/reloaded through provider");
        Check(scene->DestroyGameObject(bulk),"Cannot remove outside instances");
        provider.VertexOffset=100;++provider.Revision;object->GetTransform()->SetLocalPosition({-100,0,0});Check(render()==1&&Center(*viewport)>100,"Resource revision retained old cached culling bounds");
        provider.CustomShader=true;++provider.Revision;object->GetTransform()->SetLocalPosition({100,0,0});render();
        Check(provider.ShaderLoads==1&&viewport->GetLastError().find("Custom displacement")!=std::string::npos,"Custom vertex shader was culled using undeformed mesh bounds");
        provider.CustomShader=false;provider.Skin=true;++provider.Revision;object->GetTransform()->SetLocalPosition({0,0,0});
        auto* animator=object->AddComponent<IAnimatorComponent>();auto* bone=scene->CreateGameObject("Bone",object);bone->GetTransform()->SetLocalPosition({-100,0,0});
        Check(animator->SetNodeBindings({{"Bone",bone->GetInstanceID()}}),"Skin node binding failed");
        Check(object->AddComponent<ISkinnedMeshRendererComponent>()->SetAnimatorObjectID(object->GetInstanceID()),"Skin animator failed");
        Check(render()==1&&Center(*viewport)>100,"A visible skinned pose was culled using outside bind-pose bounds");
        delete viewport;viewport=nullptr;delete runtime;runtime=nullptr;
        std::cout<<"PASS static culling GPU: draws removed, vertex-derived bounds, editor queries, per-view camera/aspect, planes, resource revision, custom shader and skinned exemptions\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';delete viewport;delete runtime;return 1;}
}
