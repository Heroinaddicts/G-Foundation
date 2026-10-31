#include "GameSproutRuntimeExport.h"
#include "RuntimeRenderApi.h"
#include "GRHIExport.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

static void Check(bool condition,const std::string& message) {if(!condition) throw std::runtime_error(message);}
static void UInt(std::vector<std::uint8_t>& bytes,std::uint32_t value) {
    for(int shift=0;shift<32;shift+=8) bytes.push_back(static_cast<std::uint8_t>(value>>shift));
}
static void Float(std::vector<std::uint8_t>& bytes,float value) {
    std::uint32_t bits;std::memcpy(&bits,&value,4);UInt(bytes,bits);
}
class SkinProvider final : public GameSproutRuntime::IRuntimeResourceProvider {
public:
    std::uint64_t Revision=1;
    std::uint32_t BoneCount=2;
    bool Color=false,UV=false,BadIndex=false,BadWeight=false,DiagonalNormal=false;
    int MeshLoads=0;
    std::uint64_t GetRevision() const override {return Revision;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MeshRenderData& data,std::string&) override {
        ++MeshLoads;data={};data.VertexCount=3;data.VertexStride=56+(Color?16:0)+(UV?8:0);
        data.ColorOffset=Color?24:-1;data.UVOffset=UV?(Color?40:24):-1;
        data.BoneIndicesOffset=24+(Color?16:0)+(UV?8:0);data.BoneWeightsOffset=data.BoneIndicesOffset+16;
        const float positions[3][3]={{-.14f,-.14f,0},{.14f,-.14f,0},{0,.14f,0}};
        for(const auto& position:positions) {
            for(float value:position) Float(data.Bytes,value);
            Float(data.Bytes,DiagonalNormal?1:0);Float(data.Bytes,DiagonalNormal?1:0);Float(data.Bytes,DiagonalNormal?0:1);
            if(Color) for(int component=0;component<4;++component) Float(data.Bytes,1);
            if(UV) {Float(data.Bytes,.5f);Float(data.Bytes,.5f);}
            for(int influence=0;influence<4;++influence) UInt(data.Bytes,BadIndex?BoneCount:1);
            Float(data.Bytes,BadWeight?-.5f:1);Float(data.Bytes,0);Float(data.Bytes,0);Float(data.Bytes,0);
        }
        data.IndexOffset=data.Bytes.size();data.IndexCount=3;
        for(std::uint32_t index=0;index<3;++index) UInt(data.Bytes,index);
        for(std::uint32_t index=0;index<BoneCount;++index) data.Bones.push_back({index==1?"Bone":"Unused"+std::to_string(index),{}});
        data.Sections={{0,3,0}};data.LocalBounds={{0,0,0},{.14f,.14f,0}};return true;
    }
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData&,std::string&) override {return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData& data,std::string&) override {data={};return true;}
};
struct Character {
    GameSproutRuntime::IGameObject* Root=nullptr;
    GameSproutRuntime::IGameObject* Bone=nullptr;
    GameSproutRuntime::IGameObject* Mesh=nullptr;
};
static Character CreateCharacter(GameSproutRuntime::IScene& scene,float x) {
    Character character;character.Root=scene.CreateGameObject("Character");
    character.Root->GetTransform()->SetLocalPosition({x,0,0});
    auto* unused=scene.CreateGameObject("Unused",character.Root);unused->GetTransform()->SetLocalPosition({-10,0,0});
    character.Bone=scene.CreateGameObject("Bone",character.Root);
    character.Mesh=scene.CreateGameObject("Mesh",character.Root);
    auto* animator=character.Root->AddComponent<GameSproutRuntime::IAnimatorComponent>();Check(animator,"Animator factory failed");
    Check(animator->SetNodeBindings({{"Unused0",unused->GetInstanceID()},{"Bone",character.Bone->GetInstanceID()}}),"Cannot bind the palette to this character");
    character.Mesh->AddComponent<GameSproutRuntime::IMeshComponent>()->SetMesh({"0123456789abcdef0123456789abcdef",1});
    character.Mesh->AddComponent<GameSproutRuntime::IMeshRendererComponent>();
    auto* skin=character.Mesh->AddComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>();Check(skin,"Skin renderer factory failed");
    Check(skin->SetAnimatorObjectID(character.Root->GetInstanceID()),"Cannot bind skin to Animator");return character;
}
static int Pixel(const std::vector<std::uint8_t>& pixels,float worldX,float worldY=0) {
    const int x=static_cast<int>((worldX*.5f+.5f)*256),y=static_cast<int>((.5f-worldY*.5f)*256);
    return pixels[(std::size_t(y)*256+std::size_t(x))*4];
}
int main(int argc,char** argv) {
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    GameSproutRuntime::IRuntimeRenderer* renderer=nullptr;
    GRHI::GRHIApi* grhi=nullptr;GRHI::IDevice* device=nullptr;GRHI::ITexture* target=nullptr;
    SkinProvider provider;
    try {
        Check(argc==2,"Pass SDK output directory");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        grhi=ExportGRHIApi(argv[1]);if(grhi) device=grhi->CreateDevice();
        if(!device) {delete runtime;delete grhi;std::cout<<"SKIP GPU unavailable\n";return 77;}
        renderer=runtime->CreateRenderer(device,&provider);Check(renderer,"Cannot create Renderer");
        GRHI::TextureDesc description;description.Width=description.Height=256;description.Usage=GRHI::TextureUsage::ColorTarget;
        target=device->CreateTexture(description);Check(target,device->GetLastError());
        auto* scene=runtime->CreateScene("Skinned instances");auto first=CreateCharacter(*scene,-.5f),second=CreateCharacter(*scene,.5f);
        GameSproutRuntime::RenderView view;view.Scene=scene;view.ColorTarget=target;view.DrawGrid=false;view.ClearColor={0,0,0,1};
        view.Camera.Position={0,0,3};view.Camera.Projection.Projection=GameSproutRuntime::CameraProjection::Orthographic;
        view.Camera.Projection.OrthographicHeight=2;
        Check(GameSproutRuntime::TryMakeLookAt(view.Camera.Position,{0,0,0},{0,1,0},view.Camera.View),"Cannot make test camera");
        std::vector<std::uint8_t> pixels;
        Check(renderer->Render(view),renderer->GetLastError());Check(device->ReadTexture(target,pixels),device->GetLastError());
        Check(Pixel(pixels,-.5f)>100 && Pixel(pixels,.5f)>100,"Integer palette input failed to render the second bone");
        Check(provider.MeshLoads==1,"Identical meshes were loaded per instance");
        first.Bone->GetTransform()->SetLocalPosition({.3f,0,0});
        Check(renderer->Render(view),renderer->GetLastError());Check(device->ReadTexture(target,pixels),device->GetLastError());
        Check(Pixel(pixels,-.2f)>100 && Pixel(pixels,-.5f)<5 && Pixel(pixels,.5f)>100,"Per-instance palette moved a shared mesh or failed to move its vertex data");
        const int loads=provider.MeshLoads;Check(renderer->Render(view) && provider.MeshLoads==loads,"Changing pose reloaded the mesh artifact");
        GameSproutRuntime::SceneRaycastHit hit;
        Check(renderer->Raycast(scene,{{-.2f,0,3},{0,0,-1}},hit) && hit.ObjectID==first.Mesh->GetInstanceID(),"Picking did not use the current skinned pose");
        Check(!renderer->Raycast(scene,{{-.5f,0,3},{0,0,-1}},hit),"Picking still hit the old bind position");
        GameSproutRuntime::Bounds bounds;
        Check(renderer->GetWorldBounds(scene,first.Mesh->GetInstanceID(),bounds) && std::fabs(bounds.Center.X+.2f)<.001f,"Bounds ignored current bone translation");
        GameSproutRuntime::DebugDrawList outline;
        Check(renderer->AppendSelectionOutline(scene,first.Mesh->GetInstanceID(),outline,{1,1,0,1},false) && outline.Lines.size()==3,"Skinned selection outline missing");
        for(const auto& line:outline.Lines) Check(line.From.X>-.35f && line.From.X<-.05f && line.To.X>-.35f && line.To.X<-.05f,"Outline retained bind pose vertices");
        for(int variant=0;variant<4;++variant) {
            provider.Color=(variant&1)!=0;provider.UV=(variant&2)!=0;++provider.Revision;
            Check(renderer->Render(view),renderer->GetLastError());Check(device->ReadTexture(target,pixels),device->GetLastError());
            Check(Pixel(pixels,-.2f)>100,"A skinned UV/color shader variant used incorrect vertex locations");
        }
        provider.DiagonalNormal=true;++provider.Revision;
        first.Bone->GetTransform()->SetLocalScale({1,.2f,1});
        Check(renderer->Render(view),renderer->GetLastError());Check(device->ReadTexture(target,pixels),device->GetLastError());
        Check(Pixel(pixels,-.2f)>190,"Skin normals did not apply inverse transpose under nonuniform bone scale");
        first.Bone->GetTransform()->SetLocalScale({1,1,1});provider.BoneCount=129;++provider.Revision;
        Check(renderer->Render(view),renderer->GetLastError());
        Check(renderer->GetStatistics().DrawCalls==0 && renderer->GetLastError().find("128-bone")!=std::string::npos,"Oversized palette was uploaded or had no diagnostic");
        provider.BoneCount=2;provider.BadIndex=true;++provider.Revision;
        Check(renderer->Render(view) && renderer->GetStatistics().DrawCalls==0 && !renderer->GetLastError().empty(),"Out-of-range skin index reached GPU");
        provider.BadIndex=false;provider.BadWeight=true;++provider.Revision;
        Check(renderer->Render(view) && renderer->GetStatistics().DrawCalls==0 && !renderer->GetLastError().empty(),"Invalid skin weights reached GPU");
        delete renderer;delete target;delete runtime;delete device;delete grhi;
        std::cout<<"PASS actual GPU skinning, shared instances, picking/bounds/outline, variants and invalid palettes\n";return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';delete renderer;delete target;delete runtime;delete device;delete grhi;return 1;
    }
}
