#include "GameSproutRuntimeExport.h"
#include "RuntimeRenderApi.h"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace GameSproutRuntime;
namespace {
void Check(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
void UInt(std::vector<std::uint8_t>& bytes,std::uint32_t value){for(int shift=0;shift<32;shift+=8)bytes.push_back(std::uint8_t(value>>shift));}
void Float(std::vector<std::uint8_t>& bytes,float value){std::uint32_t bits;std::memcpy(&bits,&value,4);UInt(bytes,bits);}
class Provider final:public IRuntimeResourceProvider {
public:
    std::uint64_t Revision=1;
    MaterialRenderData Material;
    bool SRGB=true,NormalSRGB=false,Skin=false,UV=true,VertexColor=false;
    std::array<std::uint8_t,4> Pixel{255,255,255,255},NormalPixel{128,128,255,255};
    Provider(){Material.ShadingModel=MaterialShadingModel::Lit;Material.Roughness=1;}
    std::uint64_t GetRevision() const override{return Revision;}
    bool LoadMesh(const ResourceReference&,MeshRenderData& data,std::string&) override {
        data={};data.VertexCount=3;data.VertexStride=24+(UV?8:0)+(VertexColor?16:0)+(Skin?32:0);
        data.ColorOffset=VertexColor?24:-1;data.UVOffset=UV?(VertexColor?40:24):-1;
        if(Skin){data.BoneIndicesOffset=24+(UV?8:0)+(VertexColor?16:0);data.BoneWeightsOffset=data.BoneIndicesOffset+16;data.Bones={{"Bone",{}}};}
        const float positions[3][3]={{-.7f,-.7f,0},{.7f,-.7f,0},{0,.7f,0}};
        const float uvs[3][2]={{0,0},{1,0},{.5f,1}};
        for(int index=0;index<3;++index){for(float value:positions[index])Float(data.Bytes,value);
            Float(data.Bytes,0);Float(data.Bytes,0);Float(data.Bytes,1);
            if(VertexColor)for(int channel=0;channel<4;++channel)Float(data.Bytes,1);
            if(UV){Float(data.Bytes,uvs[index][0]);Float(data.Bytes,uvs[index][1]);}
            if(Skin){for(int influence=0;influence<4;++influence)UInt(data.Bytes,0);Float(data.Bytes,1);Float(data.Bytes,0);Float(data.Bytes,0);Float(data.Bytes,0);}}
        data.IndexOffset=data.Bytes.size();data.IndexCount=3;for(std::uint32_t index=0;index<3;++index)UInt(data.Bytes,index);
        data.LocalBounds={{0,0,0},{.7f,.7f,0}};return true;
    }
    bool LoadTexture(const ResourceReference& reference,TextureRenderData& data,std::string&) override {
        data={};data.Width=data.Height=1;data.SRGB=reference.LocalID==4?NormalSRGB:SRGB;
        const auto& pixel=reference.LocalID==4?NormalPixel:Pixel;data.Pixels.assign(pixel.begin(),pixel.end());return true;
    }
    bool LoadMaterial(const ResourceReference&,MaterialRenderData& data,std::string&) override{data=Material;return true;}
};
std::array<int,4> Center(IRuntimeViewport& renderer){std::vector<std::uint8_t> bytes;std::string error;Check(renderer.ReadPixels(bytes,error),error);
    const auto& size=renderer.GetStatistics();const auto offset=(std::size_t(size.Height/2)*size.Width+size.Width/2)*4;
    return{bytes[offset],bytes[offset+1],bytes[offset+2],bytes[offset+3]};}
}
int main(int argc,char** argv){
    IGameSproutRuntime* runtime=nullptr;IRuntimeViewport* viewport=nullptr;Provider provider;std::string error;
    try{
        Check(argc==2,"Pass Runtime binary directory");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        viewport=runtime->CreateOffscreenRenderer(128,128,&provider,error);
        if(!viewport){delete runtime;std::cout<<"SKIP GPU unavailable: "<<error<<'\n';return 77;}
        auto* scene=runtime->CreateScene("Lit GPU checks");auto* object=scene->CreateGameObject("Triangle");
        object->AddComponent<IMeshComponent>()->SetMesh({"0123456789abcdef0123456789abcdef",1});
        object->AddComponent<IMeshRendererComponent>()->SetMaterials({{"0123456789abcdef0123456789abcdef",2}});
        RenderView view;view.Scene=scene;view.DrawGrid=false;view.ClearColor={0,0,0,1};view.Camera.Position={0,0,3};
        view.Camera.Projection.Projection=CameraProjection::Orthographic;view.Camera.Projection.OrthographicHeight=2;
        Check(TryMakeLookAt(view.Camera.Position,{0,0,0},{0,1,0},view.Camera.View),"Camera failed");
        const auto render=[&]{Check(viewport->Render(view),viewport->GetLastError());return Center(*viewport);};
        auto pixel=render();Check(pixel[0]==0&&pixel[1]==0&&pixel[2]==0,"Lit invented a preview/ambient light");
        auto* sunObject=scene->CreateGameObject("Sun");auto* light=sunObject->AddComponent<ILightComponent>();Check(light,"No Light factory");
        pixel=render();const int white=pixel[0];Check(white>65&&white<100&&std::abs(pixel[1]-white)<2,"Directional light did not produce linear diffuse/specular radiance");
        auto settings=light->GetSettings();settings.Intensity=2;Check(light->SetSettings(settings),"Intensity failed");
        pixel=render();Check(std::abs(pixel[0]-2*white)<3,"Light intensity was not linear or did not update next frame");
        settings.Intensity=1;settings.Color={0,0,1,1};light->SetSettings(settings);
        pixel=render();Check(pixel[0]==0&&pixel[1]==0&&pixel[2]>65,"Light color did not reach Lit shader");
        settings.Color={1,1,1,1};settings.Enabled=false;light->SetSettings(settings);Check(render()[0]==0,"Disabled light still contributes");
        settings.Enabled=true;settings.CullingMask=1;light->SetSettings(settings);object->SetLayer(31);Check(render()[0]==0,"Light ignored object layer");
        settings.CullingMask=std::uint32_t{1}<<31;light->SetSettings(settings);Check(render()[0]>65,"High-bit light layer mask failed");object->SetLayer(0);
        settings.CullingMask=AllLayers;light->SetSettings(settings);sunObject->SetActive(false);Check(render()[0]==0,"Inactive hierarchy light still contributes");sunObject->SetActive(true);
        auto* parent=scene->CreateGameObject("Scaled light parent");Check(parent->AddChild(sunObject,false),"Light parenting failed");
        parent->GetTransform()->SetLocalScale({-2,0,5});sunObject->GetTransform()->SetLocalPosition({100,20,-300});
        Check(std::abs(render()[0]-white)<2,"Light direction depended on position, reflected scale or zero parent scale");
        sunObject->GetTransform()->SetLocalRotation({0,1,0,0});Check(render()[0]==0,"Direction ignored rotation");sunObject->GetTransform()->SetLocalRotation({});
        settings.Enabled=false;light->SetSettings(settings);provider.Material.EmissionColor={.25f,.125f,0,1};++provider.Revision;
        pixel=render();Check(std::abs(pixel[0]-64)<2&&std::abs(pixel[1]-32)<2,"Emission Color was not linear or required a light");
        provider.Material.EmissionColor={0,0,0,1};settings.Enabled=true;light->SetSettings(settings);
        provider.Material.BaseColor={.5f,.5f,.5f,1};provider.Material.Metallic=1;provider.Material.Roughness=.5f;++provider.Revision;
        const int smooth=render()[0];provider.Material.Roughness=1;++provider.Revision;
        const int rough=render()[0];Check(smooth>rough+40,"Metallic GGX roughness did not change highlight width/peak");
        provider.Material.Metallic=0;provider.Material.BaseColor={1,1,1,1};provider.Material.BaseColorTexture={"0123456789abcdef0123456789abcdef",3};
        provider.Pixel={128,128,128,255};provider.SRGB=true;++provider.Revision;const int srgb=render()[0];
        provider.SRGB=false;++provider.Revision;const int linear=render()[0];Check(srgb>10&&linear>srgb*1.6,"Color texture was not decoded exactly once by sRGB sampling");
        provider.Material.BaseColorTexture={};provider.Material.NormalTexture={"0123456789abcdef0123456789abcdef",4};++provider.Revision;
        const int flat=render()[0];Check(std::abs(flat-white)<3,"Flat tangent normal did not preserve vertex normal");
        provider.NormalPixel={255,128,128,255};++provider.Revision;const int tilted=render()[0];Check(tilted<flat/4,"Tangent normal map did not change illumination");
        provider.Material.NormalScale=0;++provider.Revision;Check(render()[0]>65,"Zero normal strength did not restore geometric normal");
        provider.Material.NormalScale=1;sunObject->GetTransform()->SetLocalRotation({0,.38268343f,0,.92387953f});++provider.Revision;
        const int towardsMappedNormal=render()[0];provider.Material.NormalTextureST={-1,1,0,0};++provider.Revision;
        Check(towardsMappedNormal>30&&render()[0]<towardsMappedNormal/5,"Mirrored normal-map UV transform did not preserve tangent handedness");
        sunObject->GetTransform()->SetLocalRotation({});provider.Material.NormalTextureST={1,1,0,0};
        provider.NormalSRGB=true;++provider.Revision;render();Check(viewport->GetStatistics().DrawCalls==0&&viewport->GetLastError().find("Linear")!=std::string::npos,"sRGB normal map reached GPU without an actionable diagnostic");
        provider.NormalSRGB=false;provider.Material.NormalTexture={};provider.Material.NormalScale=1;
        for(int variant=0;variant<4;++variant){provider.UV=(variant&1)!=0;provider.VertexColor=(variant&2)!=0;++provider.Revision;Check(render()[0]>65,"Static Lit optional vertex layout failed");}
        auto* secondLight=scene->CreateGameObject("Fill")->AddComponent<ILightComponent>();
        settings=secondLight->GetSettings();settings.Intensity=.25f;secondLight->SetSettings(settings);
        Check(std::abs(render()[0]-int(white*1.25f))<3,"Multiple directional lights were not accumulated");
        std::vector<ILightComponent*> extraLights;
        for(int index=0;index<3;++index){auto* extra=scene->CreateGameObject("Extra directional")->AddComponent<ILightComponent>();extra->SetIntensity(.25f);extraLights.push_back(extra);}
        Check(std::abs(render()[0]-int(white*1.75f))<3&&viewport->GetLastError().find("four brightest")!=std::string::npos,
            "Directional-light limit silently overran uniforms or selected the wrong lights");
        // An excluded brighter light must not consume one of this object's slots.
        extraLights.back()->SetIntensity(20);extraLights.back()->SetCullingMask(1u<<31);
        Check(std::abs(render()[0]-int(white*1.75f))<3,"Light budget was consumed before per-object mask filtering");
        for(auto* extra:extraLights)extra->SetEnabled(false);
        secondLight->SetEnabled(false);
        provider.Skin=true;provider.UV=true;++provider.Revision;auto* animator=object->AddComponent<IAnimatorComponent>();
        auto* bone=scene->CreateGameObject("Bone",object);Check(animator->SetNodeBindings({{"Bone",bone->GetInstanceID()}}),"Skin node binding failed");
        auto* skin=object->AddComponent<ISkinnedMeshRendererComponent>();Check(skin->SetAnimatorObjectID(object->GetInstanceID()),"Skin owner failed");
        Check(render()[0]>65,"Lit skinned vertex shader did not render");bone->GetTransform()->SetLocalPosition({1.5f,0,0});Check(render()[0]==0,"Lit vertex stage ignored skin palette");
        delete viewport;viewport=nullptr;delete runtime;runtime=nullptr;
        std::cout<<"PASS Lit GPU: linear colors/intensity, active/direction/layers, scale-independent lights, emission, GGX, sRGB, derivative normals, optional layouts, multiple lights, skinning\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';delete viewport;delete runtime;return 1;}
}
