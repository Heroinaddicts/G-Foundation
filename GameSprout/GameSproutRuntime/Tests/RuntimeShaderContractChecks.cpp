#include "GameSproutRuntimeExport.h"
#include "RuntimeRenderApi.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
const GameSproutRuntime::ResourceReference MeshRef{"9876543210abcdef9876543210abcdef",1};
const GameSproutRuntime::ResourceReference MaterialRef{"9876543210abcdef9876543210abcdef",2};
const GameSproutRuntime::ResourceReference ShaderRef{"9876543210abcdef9876543210abcdef",3};
class Provider final : public GameSproutRuntime::IRuntimeResourceProvider {
public:
    std::uint64_t Revision=1;
    unsigned Loads=0,Mode=0;
    std::uint64_t GetRevision() const override{return Revision;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MeshRenderData& data,std::string& error) override {
        data={};error.clear();data.VertexCount=3;data.VertexStride=24;data.IndexCount=3;data.IndexOffset=72;
        const float vertices[]={-1,-1,0,0,0,1, 1,-1,0,0,0,1, 0,1,0,0,0,1};
        const std::uint32_t indices[]={0,1,2};data.Bytes.resize(sizeof(vertices)+sizeof(indices));
        std::memcpy(data.Bytes.data(),vertices,sizeof(vertices));std::memcpy(data.Bytes.data()+sizeof(vertices),indices,sizeof(indices));
        data.LocalBounds={{0,0,0},{1,1,0}};return true;
    }
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData& data,std::string& error) override {
        data={};error="No texture fixture";return false;
    }
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData& data,std::string& error) override {
        data={};data.Shader=ShaderRef;error.clear();return true;
    }
    bool LoadShader(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::ShaderRenderData& data,std::string& error) override {
        ++Loads;data={};error.clear();data.Name="Invalid metadata fixture";
        if(Mode==0)return true;
        GameSproutRuntime::ShaderPassData pass;
        GameSproutRuntime::ShaderProgramCode code;code.EntryPoint="main";code.Bytes.resize(20);code.Bytes[0]=3;code.Bytes[1]=2;code.Bytes[2]=35;code.Bytes[3]=7;
        pass.Vertex.Codes.push_back(code);pass.Fragment.Codes.push_back(code);
        if(Mode==1)pass.LightMode="ShadowCaster";
        if(Mode==2)pass.Vertex.Uniforms.push_back({0,176,GameSproutRuntime::ShaderUniformSource::Draw,{}});
        if(Mode==3)pass.Fragment.Uniforms.push_back({0,16,GameSproutRuntime::ShaderUniformSource::Material,
            {{"OutOfBounds",GameSproutRuntime::ShaderValueType::Float4,4}}});
        if(Mode==4)pass.Fragment.Uniforms.push_back({0,16,GameSproutRuntime::ShaderUniformSource::Material,
            {{"A",GameSproutRuntime::ShaderValueType::Float4,0},{"B",GameSproutRuntime::ShaderValueType::Float,4}}});
        if(Mode==5)pass.Fragment.Uniforms.push_back({0,48,GameSproutRuntime::ShaderUniformSource::Frame,{}});
        if(Mode==6)pass.Fragment.Uniforms.push_back({0,64,GameSproutRuntime::ShaderUniformSource::Frame,
            {{"Time",GameSproutRuntime::ShaderValueType::Float4,0}}});
        if(Mode==7){
            pass.Fragment.Uniforms.push_back({0,64,GameSproutRuntime::ShaderUniformSource::Frame,{}});
            pass.Fragment.Uniforms.push_back({1,64,GameSproutRuntime::ShaderUniformSource::Frame,{}});
        }
        data.Passes.push_back(std::move(pass));return true;
    }
};
}
int main(int argc,char** argv) {
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;GameSproutRuntime::IRuntimeViewport* viewport=nullptr;
    try {
        Check(argc==2,"Pass Runtime binary directory");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        Provider provider;std::string error;viewport=runtime->CreateOffscreenRenderer(32,32,&provider,error);
        if(!viewport){delete runtime;std::cout<<"SKIP GPU unavailable: "<<error<<'\n';return 77;}
        auto* scene=runtime->CreateScene("Invalid Shader metadata");auto* object=scene->CreateGameObject("Triangle");
        object->AddComponent<GameSproutRuntime::IMeshComponent>()->SetMesh(MeshRef);
        Check(object->AddComponent<GameSproutRuntime::IMeshRendererComponent>()->SetMaterials({MaterialRef}),"Cannot set material");
        GameSproutRuntime::RenderView view;view.Scene=scene;view.DrawGrid=false;view.ClearColor={0,0,0,1};view.Camera.Position={0,0,3};
        Check(GameSproutRuntime::TryMakeLookAt(view.Camera.Position,{0,0,0},{0,1,0},view.Camera.View),"Cannot aim camera");
        for(unsigned mode=0;mode<8;++mode) {
            provider.Mode=mode;++provider.Revision;const auto before=provider.Loads;
            Check(viewport->Render(view),viewport->GetLastError());
            Check(viewport->GetStatistics().DrawCalls==0&&viewport->GetStatistics().SkippedObjects==1,"Invalid Shader silently used preview draw");
            const auto diagnostic=viewport->GetLastError();Check(!diagnostic.empty(),"Invalid Shader produced no diagnostic");
            Check(viewport->Render(view)&&provider.Loads==before+1,"Invalid Shader provider was polled every frame");
            std::vector<std::uint8_t> pixels;Check(viewport->ReadPixels(pixels,error),error);
            const auto center=(16u*32u+16u)*4u;
            Check(pixels[center]==0&&pixels[center+1]==0&&pixels[center+2]==0,"Invalid Shader drew unrelated fallback pixels");
        }
        delete viewport;viewport=nullptr;delete runtime;runtime=nullptr;
        std::cout<<"PASS invalid Shader metadata is diagnosed, cached and never rendered with preview fallback\n";return 0;
    }catch(const std::exception& failure){delete viewport;delete runtime;std::cerr<<"FAIL "<<failure.what()<<'\n';return 1;}
}
