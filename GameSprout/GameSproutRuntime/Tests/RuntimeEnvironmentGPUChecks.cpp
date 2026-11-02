#include "GameSproutRuntimeExport.h"
#include "RuntimeRenderApi.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace GameSproutRuntime;
namespace {
void Check(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
class Provider final:public IRuntimeResourceProvider {
public:
    std::uint64_t Revision=1;MaterialRenderData Material;Vector3 Normal{0,0,1};
    Provider(){Material.ShadingModel=MaterialShadingModel::Lit;}
    std::uint64_t GetRevision() const override{return Revision;}
    bool LoadMesh(const ResourceReference&,MeshRenderData& data,std::string&) override {
        data={};data.VertexCount=3;data.VertexStride=32;data.IndexCount=3;data.IndexOffset=96;data.UVOffset=24;
        const float vertices[3][8]={{-.7f,-.7f,0,Normal.X,Normal.Y,Normal.Z,0,0},{.7f,-.7f,0,Normal.X,Normal.Y,Normal.Z,1,0},
            {0,.7f,0,Normal.X,Normal.Y,Normal.Z,.5f,1}};
        const std::uint32_t indices[]={0,1,2};data.Bytes.resize(sizeof(vertices)+sizeof(indices));
        std::memcpy(data.Bytes.data(),vertices,sizeof(vertices));std::memcpy(data.Bytes.data()+sizeof(vertices),indices,sizeof(indices));
        data.LocalBounds={{0,0,0},{.7f,.7f,0}};return true;
    }
    bool LoadTexture(const ResourceReference&,TextureRenderData& data,std::string&) override{
        data={};data.Width=data.Height=1;data.SRGB=false;data.Pixels={128,255,128,255};return true;
    }
    bool LoadMaterial(const ResourceReference&,MaterialRenderData& data,std::string&) override{data=Material;return true;}
};
std::array<int,3> Center(IRuntimeViewport& viewport){std::vector<std::uint8_t> pixels;std::string error;Check(viewport.ReadPixels(pixels,error),error);
    const auto& stats=viewport.GetStatistics();const auto offset=(std::size_t(stats.Height/2)*stats.Width+stats.Width/2)*4;
    return{pixels[offset],pixels[offset+1],pixels[offset+2]};}
void Near(const std::array<int,3>& actual,Vector3 linear,const std::string& message){
    const float values[]={linear.X,linear.Y,linear.Z};
    for(int channel=0;channel<3;++channel)Check(std::abs(actual[channel]-int(std::lround(std::clamp(values[channel],0.0f,1.0f)*255)))<=2,
        message+" (channel "+std::to_string(channel)+"="+std::to_string(actual[channel])+")");
}
IGameObject* MeshObject(IScene& scene){auto* object=scene.CreateGameObject("Surface");
    object->AddComponent<IMeshComponent>()->SetMesh({"0123456789abcdef0123456789abcdef",1});
    object->AddComponent<IMeshRendererComponent>()->SetMaterials({{"0123456789abcdef0123456789abcdef",2}});return object;}
}
int main(int argc,char** argv){
    IGameSproutRuntime* runtime=nullptr;IRuntimeViewport* viewport=nullptr;IRuntimeViewport* secondViewport=nullptr;Provider provider;std::string error;
    try{
        Check(argc==2,"Pass Runtime binary directory");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        viewport=runtime->CreateOffscreenRenderer(128,128,&provider,error);
        if(!viewport){delete runtime;std::cout<<"SKIP GPU unavailable: "<<error<<'\n';return 77;}
        auto* scene=runtime->CreateScene("Environment first scene");auto* object=MeshObject(*scene);
        RenderView view;view.Scene=scene;view.DrawGrid=false;view.ClearColor={0,0,0,1};view.Camera.Position={0,0,3};
        view.Camera.Projection.Projection=CameraProjection::Orthographic;view.Camera.Projection.OrthographicHeight=2;
        Check(TryMakeLookAt(view.Camera.Position,{0,0,0},{0,1,0},view.Camera.View),"Camera failed");
        const auto render=[&]{Check(viewport->Render(view),viewport->GetLastError());Check(viewport->GetStatistics().DrawCalls==1,"Surface was unexpectedly culled");return Center(*viewport);};
        Near(render(),{},"None invented ambient illumination");
        EnvironmentLightingSettings environment;environment.Mode=AmbientLightMode::Color;environment.AmbientColor={.25f,.125f,.0625f};
        Check(scene->SetEnvironmentLighting(environment),"Cannot set scene environment");
        Near(render(),{.24f,.12f,.06f},"Flat linear ambient color or diffuse energy fraction was wrong");
        environment.Intensity=2;scene->SetEnvironmentLighting(environment);Near(render(),{.48f,.24f,.12f},"Ambient intensity was not applied exactly once");
        environment.Intensity=0;scene->SetEnvironmentLighting(environment);Near(render(),{},"Zero intensity retained cached environment");
        provider.Material.EmissionColor={.1f,.2f,0};++provider.Revision;Near(render(),{.1f,.2f,0},"Ambient zero altered emission");
        provider.Material.EmissionColor={0,0,0};provider.Material.Metallic=1;++provider.Revision;environment.Intensity=1;scene->SetEnvironmentLighting(environment);
        Near(render(),{},"Full metallic surface invented ambient diffuse or specular IBL");
        provider.Material.Metallic=.5f;++provider.Revision;Near(render(),{.12f,.06f,.03f},"Metallic diffuse energy did not attenuate correctly");
        provider.Material.Metallic=0;++provider.Revision;

        environment.Mode=AmbientLightMode::Gradient;environment.SkyColor={.4f,0,0};environment.EquatorColor={0,.2f,0};environment.GroundColor={0,0,.6f};
        scene->SetEnvironmentLighting(environment);
        const auto normal=[&](Vector3 value){provider.Normal=value;++provider.Revision;return render();};
        Near(normal({0,1,0}),{.384f,0,0},"Gradient sky did not use world +Y");
        Near(normal({0,-1,0}),{0,0,.576f},"Gradient ground did not use world -Y");
        Near(normal({0,0,1}),{0,.192f,0},"Gradient horizon did not use equator");
        Near(normal({0,.5f,.8660254f}),{.192f,.096f,0},"Gradient equator-to-sky interpolation was wrong");

        environment.Mode=AmbientLightMode::SphericalHarmonics;environment.SHCoefficients={};environment.SHCoefficients[0]={.25f,.125f,.0625f};
        scene->SetEnvironmentLighting(environment);Near(normal({0,0,1}),{.24f,.12f,.06f},"SH constant was normalized or divided by pi a second time");
        environment.SHCoefficients[0]={-.1f,-.1f,-.1f};environment.SHCoefficients[2]={.3f,.3f,.3f};scene->SetEnvironmentLighting(environment);
        Near(render(),{.192f,.192f,.192f},"Signed SH coefficients were clamped before the complete sum");
        Near(normal({0,0,-1}),{},"Negative evaluated SH radiance was not clamped");
        environment.Intensity=2;scene->SetEnvironmentLighting(environment);Near(normal({0,0,1}),{.384f,.384f,.384f},"SH intensity was applied more than once");
        environment.Intensity=1;environment.SHCoefficients={};
        environment.SHCoefficients[0]={.2f,.2f,.2f};environment.SHCoefficients[1]={.1f,0,0};environment.SHCoefficients[2]={0,.15f,0};
        environment.SHCoefficients[3]={0,0,.1f};environment.SHCoefficients[4]={.08f,0,0};environment.SHCoefficients[5]={0,.04f,0};
        environment.SHCoefficients[6]={0,0,.03f};environment.SHCoefficients[7]={-.05f,0,0};environment.SHCoefficients[8]={0,-.06f,0};
        scene->SetEnvironmentLighting(environment);
        Near(normal({1,0,0}),{.192f,.1344f,.2592f},"SH +X basis order was wrong");
        Near(normal({-1,0,0}),{.192f,.1344f,.0672f},"SH -X sign was wrong");
        Near(normal({0,1,0}),{.288f,.2496f,.1632f},"SH +Y basis order was wrong");
        Near(normal({0,-1,0}),{.096f,.2496f,.1632f},"SH -Y sign was wrong");
        Near(normal({0,0,1}),{.192f,.336f,.2496f},"SH +Z basis order was wrong");
        Near(normal({0,0,-1}),{.192f,.048f,.2496f},"SH -Z sign was wrong");
        Near(normal({.70710678f,.70710678f,0}),Vector3{.31071068f,.2f,.24071068f}*.96f,"SH xy/quadratic basis was wrong");
        Near(normal({0,.70710678f,.70710678f}),Vector3{.27071068f,.35606602f,.215f}*.96f,"SH yz/quadratic basis was wrong");
        Near(normal({.70710678f,0,.70710678f}),Vector3{.175f,.27606602f,.28571068f}*.96f,"SH xz/quadratic basis was wrong");

        provider.Normal={0,0,1};++provider.Revision;object->GetTransform()->SetLocalRotation({0,.70710678f,0,.70710678f});
        view.Camera.Position={3,0,0};Check(TryMakeLookAt(view.Camera.Position,{0,0,0},{0,1,0},view.Camera.View),"Rotated surface camera failed");
        Near(render(),{.192f,.1344f,.2592f},"SH evaluated local rather than transformed world normal");
        object->GetTransform()->SetLocalRotation({});view.Camera.Position={0,0,3};TryMakeLookAt(view.Camera.Position,{0,0,0},{0,1,0},view.Camera.View);
        environment.Mode=AmbientLightMode::Gradient;scene->SetEnvironmentLighting(environment);
        provider.Material.NormalTexture={"0123456789abcdef0123456789abcdef",4};++provider.Revision;
        Near(render(),{.384f,0,0},"Environment ignored the final normal-map world normal");
        provider.Material.NormalScale=0;++provider.Revision;Near(render(),{0,.192f,0},"Normal strength zero did not restore environment horizon");
        provider.Material.NormalTexture={};provider.Material.NormalScale=1;++provider.Revision;

        auto* secondScene=runtime->CreateScene("Environment second scene");MeshObject(*secondScene);
        EnvironmentLightingSettings other;other.Mode=AmbientLightMode::Color;other.AmbientColor={0,0,.5f};Check(secondScene->SetEnvironmentLighting(other),"Second scene environment failed");
        environment.Mode=AmbientLightMode::Color;environment.AmbientColor={.5f,0,0};scene->SetEnvironmentLighting(environment);
        Near(render(),{.48f,0,0},"First scene color failed");
        auto otherView=view;otherView.Scene=secondScene;Check(viewport->Render(otherView),viewport->GetLastError());Near(Center(*viewport),{0,0,.48f},"Scene switch retained another environment");
        Near(render(),{.48f,0,0},"Returning to first scene retained another environment");
        secondViewport=runtime->CreateOffscreenRenderer(64,64,&provider,error);Check(secondViewport,error);
        Check(secondViewport->Render(otherView),secondViewport->GetLastError());Near(Center(*secondViewport),{0,0,.48f},"Independent viewport did not use its scene environment");
        auto angledView=view;angledView.Camera.Position={1,.5f,3};TryMakeLookAt(angledView.Camera.Position,{0,0,0},{0,1,0},angledView.Camera.View);
        Check(viewport->Render(angledView),viewport->GetLastError());Near(Center(*viewport),{.48f,0,0},"Diffuse environment incorrectly depended on free/game camera direction");

        provider.Material.ShadingModel=MaterialShadingModel::Preview;++provider.Revision;const auto preview=render();
        environment.Mode=AmbientLightMode::None;scene->SetEnvironmentLighting(environment);Check(render()==preview,"Scene environment altered built-in Preview shading");
        provider.Material.ShadingModel=MaterialShadingModel::Lit;provider.Material.EmissionColor={.1f,0,0};++provider.Revision;
        environment.Mode=AmbientLightMode::Color;environment.Intensity=std::numeric_limits<float>::max();environment.AmbientColor={2,2,2};scene->SetEnvironmentLighting(environment);
        Near(render(),{.1f,0,0},"Derived environment overflow corrupted geometry/emission instead of disabling ambient");
        Check(viewport->GetLastError().find("finite shader precision")!=std::string::npos,"Derived HDR overflow had no environment diagnostic");
        delete secondViewport;secondViewport=nullptr;delete viewport;viewport=nullptr;delete runtime;runtime=nullptr;
        std::cout<<"PASS actual GPU diffuse environment: linear modes, gradient, signed SH9/world basis, energy/intensity, mapped normals, camera/scene/viewport isolation, preview/emission and overflow\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';delete secondViewport;delete viewport;delete runtime;return 1;}
}
