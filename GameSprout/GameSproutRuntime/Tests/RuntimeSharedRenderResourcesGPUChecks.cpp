#include "GameSproutRuntimeExport.h"
#include "RuntimeRenderApi.h"
#include "../Renderer/Shaders/GeneratedShaders.h"
#include <array>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>

using namespace GameSproutRuntime;
namespace {
const ResourceReference MeshRef{"aabbccddeeff00112233445566778899",1};
const ResourceReference MaterialRef{"aabbccddeeff00112233445566778899",2};
const ResourceReference TextureRef{"aabbccddeeff00112233445566778899",3};
const ResourceReference ShaderRef{"aabbccddeeff00112233445566778899",4};
void Check(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
class Provider final:public IRuntimeResourceProvider {
public:
    std::uint64_t Revision=1;
    unsigned MeshLoads=0,TextureLoads=0,MaterialLoads=0,ShaderLoads=0;
    bool Custom=false,MissingMesh=false;
    std::array<std::uint8_t,4> Pixel{255,0,0,255};
    std::uint64_t GetRevision()const override{return Revision;}
    bool LoadMesh(const ResourceReference&,MeshRenderData& data,std::string& error)override {
        ++MeshLoads;data={};error.clear();if(MissingMesh){error="Missing mesh fixture";return false;}
        data.VertexCount=3;data.VertexStride=32;data.IndexCount=3;data.IndexOffset=96;data.UVOffset=24;
        const float vertices[]={-.8f,-.8f,0,0,0,1,0,0, .8f,-.8f,0,0,0,1,1,0, 0,.8f,0,0,0,1,.5f,1};
        const std::uint32_t indices[]={0,1,2};data.Bytes.resize(sizeof(vertices)+sizeof(indices));
        std::memcpy(data.Bytes.data(),vertices,sizeof(vertices));std::memcpy(data.Bytes.data()+sizeof(vertices),indices,sizeof(indices));
        data.LocalBounds={{},{.8f,.8f,0}};return true;
    }
    bool LoadTexture(const ResourceReference&,TextureRenderData& data,std::string& error)override {
        ++TextureLoads;data={};data.Width=data.Height=1;data.SRGB=false;
        data.Pixels.assign(Pixel.begin(),Pixel.end());error.clear();return true;
    }
    bool LoadMaterial(const ResourceReference&,MaterialRenderData& data,std::string& error)override {
        ++MaterialLoads;data={};error.clear();
        if(!Custom){data.BaseColorTexture=TextureRef;return true;}
        data.Shader=ShaderRef;data.Textures.push_back({"Base",TextureRef});
        // Fixed model/normal and orthographic view-projection matrices reuse the
        // offline shader. From world +Z looking back, Unity camera right is -X.
        for(unsigned index=0;index<14;++index){
            MaterialPropertyData value;value.Name="Vector"+std::to_string(index);value.Type=ShaderValueType::Float4;
            if(index<12)value.Numbers[index%4]=index==4?-1:1;
            else if(index==12)for(auto& number:value.Numbers)number=1;
            else {value.Numbers[0]=value.Numbers[1]=1;}
            data.Properties.push_back(value);
        }
        return true;
    }
    template<std::size_t N> void Code(ShaderProgramData& program,ShaderProgramFormat format,ShaderProgramPlatform platform,
        const unsigned char(&bytes)[N],const char* entry) {
        ShaderProgramCode code;code.Format=format;code.Platform=platform;code.EntryPoint=entry;
        code.Bytes.assign(bytes,bytes+N);program.Codes.push_back(std::move(code));
    }
    bool LoadShader(const ResourceReference&,ShaderRenderData& data,std::string& error)override {
        ++ShaderLoads;data={};error.clear();data.Name="Shared shader fixture";ShaderPassData pass;
        ShaderUniformBlock block;block.Size=224;
        for(unsigned index=0;index<14;++index)block.Members.push_back({"Vector"+std::to_string(index),ShaderValueType::Float4,index*16});
        pass.Vertex.Uniforms.push_back(std::move(block));pass.Fragment.Textures.push_back({"Base",0});
#define ADD(Name,Program) \
        Code(Program,ShaderProgramFormat::SPIRV,ShaderProgramPlatform::Vulkan,EmbeddedShaders::Name##SPIRV,EmbeddedShaders::Name##SPIRVEntry); \
        Code(Program,ShaderProgramFormat::MSL,ShaderProgramPlatform::Apple,EmbeddedShaders::Name##MSL,EmbeddedShaders::Name##MSLEntry); \
        Code(Program,ShaderProgramFormat::DXIL,ShaderProgramPlatform::Direct3D12,EmbeddedShaders::Name##DXIL,EmbeddedShaders::Name##DXILEntry)
        ADD(MeshNoColorVertex,pass.Vertex);ADD(MeshFragment,pass.Fragment);
#undef ADD
        data.Passes.push_back(std::move(pass));return true;
    }
    std::array<unsigned,4> Loads()const{return {MeshLoads,TextureLoads,MaterialLoads,ShaderLoads};}
};
std::array<unsigned,4> Pixel(IRuntimeViewport& viewport,const RenderView& view) {
    Check(viewport.Render(view),viewport.GetLastError());
    std::string error;std::vector<std::uint8_t> pixels;Check(viewport.ReadPixels(pixels,error),error);
    const auto& size=viewport.GetStatistics();const auto offset=(std::size_t(size.Height/2)*size.Width+size.Width/2)*4;
    return {pixels[offset],pixels[offset+1],pixels[offset+2],pixels[offset+3]};
}
void Red(const std::array<unsigned,4>& value){Check(value[0]>100&&value[1]==0&&value[2]==0,"Expected red shared texture");}
void Blue(const std::array<unsigned,4>& value){Check(value[2]>100&&value[0]==0&&value[1]==0,"Expected blue shared texture");}
}
int main(int argc,char** argv) {
    IGameSproutRuntime* runtime=nullptr;IRuntimeViewport *first=nullptr,*second=nullptr,*other=nullptr,*later=nullptr;
    Provider provider,separate;
    try {
        Check(argc==2,"Pass Runtime binary directory");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        std::string error;first=runtime->CreateOffscreenRenderer(64,64,&provider,error);
        if(!first){delete runtime;std::cout<<"SKIP GPU unavailable: "<<error<<'\n';return 77;}
        second=runtime->CreateOffscreenRenderer(96,64,&provider,error);Check(second,error);
        auto* scene=runtime->CreateScene("Shared GPU resources");auto* object=scene->CreateGameObject("Triangle");
        object->AddComponent<IMeshComponent>()->SetMesh(MeshRef);
        Check(object->AddComponent<IMeshRendererComponent>()->SetMaterials({MaterialRef}),"Cannot bind material");
        RenderView view;view.Scene=scene;view.DrawGrid=false;view.ClearColor={0,0,0,1};view.Camera.Position={0,0,3};
        view.Camera.Projection.Projection=CameraProjection::Orthographic;view.Camera.Projection.OrthographicHeight=2;
        Check(TryMakeLookAt(view.Camera.Position,{0,0,0},{0,1,0},view.Camera.View),"Cannot aim shared-resource camera");
        Red(Pixel(*first,view));Check(provider.Loads()==std::array<unsigned,4>{1,1,1,0},"First viewport did not load each fixture once");
        const auto firstLoads=provider.Loads();Red(Pixel(*second,view));
        Check(provider.Loads()==firstLoads,"Second viewport reread/uploaded shared mesh, texture or material");
        Check(second->Resize(48,96,error),error);Red(Pixel(*second,view));
        Check(provider.Loads()==firstLoads,"Resizing a private target invalidated shared resources");
        auto clear=view;clear.DrawScene=false;Check(first->Render(clear),first->GetLastError());Red(Pixel(*first,view));
        Check(provider.Loads()==firstLoads,"Clear-only frame invalidated shared resources");

        separate.Pixel={0,255,0,255};other=runtime->CreateOffscreenRenderer(64,64,&separate,error);Check(other,error);
        const auto green=Pixel(*other,view);Check(green[1]>100&&green[0]==0&&green[2]==0,"Different provider reused another provider's GUID cache");
        Check(separate.Loads()==std::array<unsigned,4>{1,1,1,0}&&provider.Loads()==firstLoads,"Provider cache keys or read counts were not isolated");
        delete other;other=nullptr;

        provider.Pixel={0,0,255,255};++provider.Revision;
        Blue(Pixel(*second,view));const auto secondLoads=provider.Loads();
        Check(secondLoads==std::array<unsigned,4>{2,2,2,0},"Resource revision did not rebuild the shared generation once");
        Blue(Pixel(*first,view));Check(provider.Loads()==secondLoads,"Sibling viewport rebuilt the same generation or dereferenced stale resource pointers");
        delete first;first=nullptr;Blue(Pixel(*second,view));
        Check(provider.Loads()==secondLoads,"Deleting the first viewport released resources still used by its sibling");

        later=runtime->CreateOffscreenRenderer(64,64,&provider,error);Check(later,error);
        provider.Custom=true;++provider.Revision;
        Blue(Pixel(*second,view));const auto shaderLoads=provider.Loads();
        Check(shaderLoads[3]==1&&second->GetStatistics().DrawCalls==1,"Custom Shader did not load/render successfully");
        Blue(Pixel(*later,view));Check(provider.Loads()==shaderLoads,"Second custom-Shader viewport rebuilt canonical geometry, material bindings or native program");
        Blue(Pixel(*second,view));Check(provider.Loads()==shaderLoads,"Repeated custom-Shader rendering reloaded resources");
        delete second;second=nullptr;Blue(Pixel(*later,view));
        Check(provider.Loads()==shaderLoads,"Shared Shader resources depended on the first viewport's lifetime");
        delete later;later=nullptr;

        // Cache leases must not pin a destroyed provider by address forever.
        // No renderer remains: a subsequent renderer must start a fresh cache.
        provider.Custom=false;const auto beforeFresh=provider.Loads();
        first=runtime->CreateOffscreenRenderer(64,64,&provider,error);Check(first,error);Blue(Pixel(*first,view));
        Check(provider.MeshLoads==beforeFresh[0]+1&&provider.TextureLoads==beforeFresh[1]+1&&provider.MaterialLoads==beforeFresh[2]+1,
            "Last viewport destruction left a provider-address cache permanently alive");
        delete first;first=nullptr;
        Check(!runtime->CreateOffscreenRenderer(0xffffffffu,0xffffffffu,&provider,error),"Invalid viewport size unexpectedly succeeded");
        first=runtime->CreateOffscreenRenderer(64,64,&provider,error);Check(first,error);Blue(Pixel(*first,view));
        delete first;first=nullptr;
        delete runtime;runtime=nullptr;
        std::cout<<"PASS shared GPU resources: same-provider reuse, separate providers, revision generations, resize/clear isolation, Shader bindings and viewport/failed-construction lifetimes\n";return 0;
    }catch(const std::exception& failure){delete later;delete other;delete second;delete first;delete runtime;std::cerr<<"FAIL "<<failure.what()<<'\n';return 1;}
}
