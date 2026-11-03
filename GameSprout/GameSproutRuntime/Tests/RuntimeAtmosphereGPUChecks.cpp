#include "GameSproutRuntimeExport.h"
#include "RuntimeRenderApi.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace GameSproutRuntime;
namespace {
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
class Provider final:public IRuntimeResourceProvider {
public:
    std::uint64_t GetRevision() const override{return 1;}
    bool LoadMesh(const ResourceReference&,MeshRenderData& data,std::string&) override {
        data={};data.VertexCount=3;data.VertexStride=32;data.IndexCount=3;data.IndexOffset=96;data.UVOffset=24;
        const float vertices[][8]={{-.7f,-.7f,0,0,0,1,0,0},{.7f,-.7f,0,0,0,1,1,0},{0,.7f,0,0,0,1,.5f,1}};
        const std::uint32_t indices[]={0,1,2};data.Bytes.resize(sizeof(vertices)+sizeof(indices));
        std::memcpy(data.Bytes.data(),vertices,sizeof(vertices));std::memcpy(data.Bytes.data()+sizeof(vertices),indices,sizeof(indices));
        data.LocalBounds={{0,0,0},{.7f,.7f,0}};return true;
    }
    bool LoadTexture(const ResourceReference&,TextureRenderData&,std::string&) override{return false;}
    bool LoadMaterial(const ResourceReference&,MaterialRenderData& data,std::string&) override {
        data={};data.ShadingModel=MaterialShadingModel::Lit;data.EmissionColor={1,0,0,1};return true;
    }
};
std::vector<std::uint8_t> Pixels(IRuntimeViewport& viewport) {
    std::vector<std::uint8_t> pixels;std::string error;Check(viewport.ReadPixels(pixels,error),error);return pixels;
}
void Center(IRuntimeViewport& viewport,Vector3 expected,const std::string& message) {
    const auto pixels=Pixels(viewport);const auto& stats=viewport.GetStatistics();
    const auto offset=(std::size_t(stats.Height/2)*stats.Width+stats.Width/2)*4;
    const float colors[]={expected.X,expected.Y,expected.Z};
    for(std::size_t channel=0;channel<3;++channel)
        Check(std::abs(int(pixels[offset+channel])-int(std::lround(std::clamp(colors[channel],0.0f,1.0f)*255)))<=2,message);
}
IGameObject* Mesh(IScene& scene,const char* name) {
    auto* object=scene.CreateGameObject(name);object->AddComponent<IMeshComponent>()->SetMesh({"0123456789abcdef0123456789abcdef",1});
    Check(object->AddComponent<IMeshRendererComponent>()->SetMaterials({{"0123456789abcdef0123456789abcdef",2}}),"Material failed");return object;
}
}
int main(int argc,char** argv) {
    IGameSproutRuntime* runtime=nullptr;IRuntimeViewport* viewport=nullptr;Provider provider;
    try {
        Check(argc==2,"Pass Runtime binary directory");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        std::string error;viewport=runtime->CreateOffscreenRenderer(96,96,&provider,error);
        if(!viewport){delete runtime;std::cout<<"SKIP GPU unavailable: "<<error<<'\n';return 77;}
        auto* scene=runtime->CreateScene("Atmosphere");auto* object=Mesh(*scene,"Surface");
        RenderView view;view.Scene=scene;view.DrawGrid=false;view.ClearColor={0,0,0,1};
        view.Camera.Projection.Projection=CameraProjection::Orthographic;view.Camera.Projection.OrthographicHeight=2;
        const auto camera=[&](Vector3 position,Vector3 target,Vector3 up=Vector3{0,1,0}) {
            view.Camera.Position=position;Check(TryMakeLookAt(position,target,up,view.Camera.View),"Camera failed");
        };
        const auto render=[&]{Check(viewport->Render(view),viewport->GetLastError());};
        EnvironmentLightingSettings environment;auto& atmosphere=environment.Atmosphere;
        atmosphere.Enabled=true;atmosphere.DrawSky=false;atmosphere.FogStart=5;atmosphere.FogEnd=10;
        atmosphere.FogMaxOpacity=1;atmosphere.FogColor={0,.25f,.5f,1};
        Check(scene->SetEnvironmentLighting(environment),"Atmosphere failed");
        camera({0,0,3},{});render();Center(*viewport,{1,0,0},"Fog affected near surfaces");
        camera({0,0,15},{});render();Center(*viewport,{0,.25f,.5f},"Fog failed beyond end distance");
        atmosphere.FogMaxOpacity=.5f;scene->SetEnvironmentLighting(environment);render();
        Center(*viewport,{.5f,.125f,.25f},"Maximum fog opacity was ignored");
        atmosphere.Enabled=false;scene->SetEnvironmentLighting(environment);render();Center(*viewport,{1,0,0},"Disabled fog retained stale uniforms");

        atmosphere.Enabled=true;atmosphere.DrawSky=true;atmosphere.FogMaxOpacity=0;
        atmosphere.ZenithColor={.1f,.2f,.7f,1};atmosphere.HorizonColor={.6f,.7f,.8f,1};
        scene->SetEnvironmentLighting(environment);camera({0,0,3},{});render();
        Center(*viewport,{1,0,0},"Sky overwrote scene geometry or depth");
        object->SetActive(false);render();Center(*viewport,{.6f,.7f,.8f},"Horizontal sky ray did not use horizon color");
        Check(viewport->GetStatistics().DrawCalls==1,"Sky should use exactly one fullscreen draw");
        camera({0,0,0},{0,1,0},{0,0,1});render();Center(*viewport,{.1f,.2f,.7f},"Sky failed to track upward camera rotation");
        view.Camera.Projection.Projection=CameraProjection::Perspective;camera({0,0,0},{0,.3f,1});render();
        const auto beforeTranslation=Pixels(*viewport);
        camera({100,200,300},{100,200.3f,301});render();
        const auto afterTranslation=Pixels(*viewport);Check(beforeTranslation.size()==afterTranslation.size(),"Sky extent changed");
        for(std::size_t index=0;index<beforeTranslation.size();++index)
            Check(std::abs(int(beforeTranslation[index])-int(afterTranslation[index]))<=1,"Sky moved with camera translation");

        // The same atmosphere uniform is used for ordinary and instanced Lit.
        object->SetActive(true);object->GetTransform()->SetLocalPosition({-.5f,0,0});
        auto* sibling=Mesh(*scene,"Sibling");sibling->GetTransform()->SetLocalPosition({.5f,0,0});
        atmosphere.DrawSky=false;atmosphere.FogMaxOpacity=.65f;atmosphere.FogStart=1;atmosphere.FogEnd=10;
        scene->SetEnvironmentLighting(environment);view.Camera.Projection.Projection=CameraProjection::Orthographic;
        view.Camera.Projection.OrthographicHeight=3;camera({0,0,7},{});
        view.EnableGPUInstancing=false;render();const auto ordinary=Pixels(*viewport);
        view.EnableGPUInstancing=true;render();const auto instanced=Pixels(*viewport);
        Check(viewport->GetStatistics().InstancedInstances==2,"Atmosphere fixture failed to instance");
        Check(ordinary==instanced,"Instanced fog differs from ordinary Lit");
        auto* other=runtime->CreateScene("Unconfigured");view.Scene=other;render();Center(*viewport,{},"Atmosphere leaked across scenes");
        delete viewport;viewport=nullptr;delete runtime;runtime=nullptr;
        std::cout<<"PASS atmosphere GPU: near/far/disabled fog, opacity, sky depth, camera rotation/translation, scene isolation and instancing\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';delete viewport;delete runtime;return 1;}
}
