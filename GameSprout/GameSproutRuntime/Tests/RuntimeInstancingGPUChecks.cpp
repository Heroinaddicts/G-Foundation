#include "GameSproutRuntimeExport.h"
#include "RuntimeRenderApi.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>

using namespace GameSproutRuntime;
namespace {
const char* const Guid="0123456789abcdef0123456789abcdef";
void Check(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
void UInt(std::vector<std::uint8_t>& bytes,std::uint32_t value){
    for(int shift=0;shift<32;shift+=8)bytes.push_back(static_cast<std::uint8_t>(value>>shift));
}
void Float(std::vector<std::uint8_t>& bytes,float value){std::uint32_t bits;std::memcpy(&bits,&value,4);UInt(bytes,bits);}

class Provider final:public IRuntimeResourceProvider {
public:
    std::uint64_t Revision=1;
    unsigned MeshLoads=0,MaterialLoads=0;
    Vector3 Normal{0,0,1};
    std::map<std::uint64_t,MaterialRenderData> Materials;
    Provider(){
        Materials[100].BaseColor={.8f,.4f,.2f,1};
        Materials[101].BaseColor={.2f,.7f,.5f,1};
        Materials[102].ShadingModel=MaterialShadingModel::Lit;
        Materials[102].Roughness=.65f;
    }
    std::uint64_t GetRevision() const override{return Revision;}
    bool LoadMesh(const ResourceReference& reference,MeshRenderData& data,std::string&) override {
        ++MeshLoads;data={};
        // IDs 1..4 cover all optional color/UV layouts; ID 5 has two independently
        // material-addressable sections, not a synthetic split of one draw call.
        const auto layout=reference.LocalID==5?3u:static_cast<unsigned>(reference.LocalID-1);
        const bool color=(layout&1)!=0,uv=(layout&2)!=0;
        data.VertexCount=reference.LocalID==5?6:3;
        data.VertexStride=24+(color?16:0)+(uv?8:0);
        data.ColorOffset=color?24:-1;data.UVOffset=uv?(color?40:24):-1;
        for(unsigned index=0;index<data.VertexCount;++index){
            const unsigned corner=index%3;
            const float offset=reference.LocalID==5?(index<3?-.4f:.4f):0;
            const Vector3 positions[]={{-.3f+offset,-.3f,0},{.3f+offset,-.3f,0},{offset,.35f,0}};
            Float(data.Bytes,positions[corner].X);Float(data.Bytes,positions[corner].Y);Float(data.Bytes,positions[corner].Z);
            Float(data.Bytes,Normal.X);Float(data.Bytes,Normal.Y);Float(data.Bytes,Normal.Z);
            if(color){Float(data.Bytes,.6f);Float(data.Bytes,1);Float(data.Bytes,.8f);Float(data.Bytes,1);}
            if(uv){Float(data.Bytes,corner==1?1:corner==2?.5f:0);Float(data.Bytes,corner==2?1:0);}
        }
        data.IndexOffset=data.Bytes.size();data.IndexCount=data.VertexCount;
        for(unsigned index=0;index<data.IndexCount;++index)UInt(data.Bytes,index);
        data.Sections=reference.LocalID==5?std::vector<MeshSection>{{0,3,0},{3,3,1}}:std::vector<MeshSection>{{0,3,0}};
        data.LocalBounds={{0,0,0},{reference.LocalID==5?.7f:.3f,.35f,0}};return true;
    }
    bool LoadTexture(const ResourceReference& reference,TextureRenderData& data,std::string&) override {
        data={};data.Width=data.Height=2;data.SRGB=false;
        const std::array<std::uint8_t,16> first{255,128,64,255,64,255,128,255,128,64,255,255,255,255,255,255};
        const std::array<std::uint8_t,16> second{64,128,255,255,255,64,128,255,128,255,64,255,128,128,128,255};
        const auto& pixels=reference.LocalID==201?first:second;data.Pixels.assign(pixels.begin(),pixels.end());return true;
    }
    bool LoadMaterial(const ResourceReference& reference,MaterialRenderData& data,std::string& error) override {
        ++MaterialLoads;const auto found=Materials.find(reference.LocalID);
        if(found==Materials.end()){data={};error="Test material not found";return false;}
        data=found->second;return true;
    }
};

RenderView View(IScene* scene,float height=20){
    RenderView view;view.Scene=scene;view.DrawGrid=false;view.ClearColor={0,0,0,1};view.Camera.Position={0,0,12};
    view.Camera.Projection.Projection=CameraProjection::Orthographic;view.Camera.Projection.OrthographicHeight=height;
    view.Camera.Projection.NearPlane=.1f;view.Camera.Projection.FarPlane=50;
    Check(TryMakeLookAt(view.Camera.Position,{0,0,0},{0,1,0},view.Camera.View),"Cannot aim instancing camera");return view;
}
IGameObject* Object(IScene& scene,std::uint64_t mesh,std::uint64_t material,Vector3 position){
    auto* object=scene.CreateGameObject("Instance");Check(object,"Cannot create instance");
    object->GetTransform()->SetLocalPosition(position);
    auto* component=object->AddComponent<IMeshComponent>();Check(component,"Cannot create mesh component");component->SetMesh({Guid,mesh});
    Check(object->AddComponent<IMeshRendererComponent>()->SetMaterials({{Guid,material},{Guid,101}}),"Cannot bind instance material");return object;
}
std::vector<IGameObject*> Grid(IScene& scene,unsigned count,std::uint64_t mesh=1,std::uint64_t material=100){
    std::vector<IGameObject*> objects;objects.reserve(count);
    for(unsigned index=0;index<count;++index)
        objects.push_back(Object(scene,mesh,material,{(float(index%10)-4.5f)*1.8f,(float(index/10)-4.5f)*1.8f,0}));
    return objects;
}
std::vector<std::uint8_t> Pixels(IRuntimeViewport& viewport,const RenderView& view){
    Check(viewport.Render(view),viewport.GetLastError());std::vector<std::uint8_t> result;std::string error;
    Check(viewport.ReadPixels(result,error),error);Check(!result.empty(),"GPU readback was empty");return result;
}
void Equivalent(const std::vector<std::uint8_t>& actual,const std::vector<std::uint8_t>& expected,const std::string& message){
    Check(actual.size()==expected.size(),message+": readback dimensions differ");
    std::size_t different=0;
    for(std::size_t index=0;index<actual.size();index+=4){
        bool outside=false;for(unsigned channel=0;channel<4;++channel)
            outside|=std::abs(int(actual[index+channel])-int(expected[index+channel]))>2;
        different+=outside?1:0;
    }
    // Permit a handful of raster-edge pixels affected by native FMA contraction,
    // while checking every interior pixel and all color channels.
    Check(different<=std::max<std::size_t>(8,actual.size()/16384),message+": "+std::to_string(different)+" pixels differ");
}
std::array<int,3> Sample(const std::vector<std::uint8_t>& pixels,const RenderStatistics& size,float x,float y,float height){
    const float width=height*float(size.Width)/float(size.Height);
    // This camera stands on +Z and looks back toward the origin. Its Unity
    // transform is turned 180 degrees, so screen-right is world -X.
    const auto px=std::clamp(int((.5f-x/width)*size.Width),0,int(size.Width)-1);
    const auto py=std::clamp(int((.5f-y/height)*size.Height),0,int(size.Height)-1);
    const auto offset=(std::size_t(py)*size.Width+std::size_t(px))*4;
    return {pixels[offset],pixels[offset+1],pixels[offset+2]};
}
void CompareSingleDraws(IRuntimeViewport& viewport,const RenderView& view,const std::vector<std::uint8_t>& instanced,const std::string& message){
    auto individual=view;individual.EnableGPUInstancing=false;
    Equivalent(Pixels(viewport,individual),instanced,message);
    Check(viewport.GetStatistics().InstancedDrawCalls==0&&viewport.GetStatistics().InstancedInstances==0,
        message+": view opt-out still emitted instanced draws");
}
}

int main(int argc,char** argv){
    IGameSproutRuntime* runtime=nullptr;IRuntimeViewport* viewport=nullptr;IRuntimeViewport* secondViewport=nullptr;
    Provider provider;std::string error;
    try{
        Check(argc==2,"Pass Runtime binary directory");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        viewport=runtime->CreateOffscreenRenderer(256,256,&provider,error);
        if(!viewport){delete runtime;std::cout<<"SKIP GPU unavailable: "<<error<<'\n';return 77;}
        auto* scene=runtime->CreateScene("Automatic instancing");auto nodes=Grid(*scene,100);auto view=View(scene);
        auto pixels=Pixels(*viewport,view);auto stats=viewport->GetStatistics();
        Check(stats.VisibleObjects==100&&stats.DrawCalls==1&&stats.InstancedDrawCalls==1&&stats.InstancedInstances==100&&stats.SavedDrawCalls==99,
            "100 shared meshes/materials did not become one 100-instance GPU draw");
        Check(stats.Triangles==100&&stats.InstanceBufferUploads==1&&stats.ResidentInstanceBytes>=100*128,
            "Instance draw triangle accounting or resident upload statistics are wrong");
        unsigned colored=0;for(std::size_t index=0;index<pixels.size();index+=4)colored+=pixels[index]>0?1:0;
        Check(colored>1000,"Instanced draw submitted commands but did not render the complete instance set");
        Pixels(*viewport,view);Check(viewport->GetStatistics().InstanceBufferUploads==0,"Unchanged instances were uploaded again");
        CompareSingleDraws(*viewport,view,pixels,"100 instances differ from individual draws");
        Check(viewport->GetStatistics().DrawCalls==100,"View opt-out did not restore 100 independent draws");
        Pixels(*viewport,view);
        const auto loads=provider.MeshLoads;nodes.front()->GetTransform()->SetLocalPosition({-8,-8,0});
        const auto moved=Pixels(*viewport,view);Check(viewport->GetStatistics().InstanceBufferUploads==1&&provider.MeshLoads==loads,
            "Moving one instance reloaded shared geometry or failed to update its instance buffer");
        CompareSingleDraws(*viewport,view,moved,"Moved instance transform was stale");
        Check(scene->DestroyGameObject(nodes.back()),"Cannot delete cached instance");nodes.pop_back();
        pixels=Pixels(*viewport,view);Check(viewport->GetStatistics().InstancedInstances==99&&viewport->GetStatistics().InstanceBufferUploads==1,
            "Deleted instance remained in GPU data");CompareSingleDraws(*viewport,view,pixels,"Deleted instance still rendered");
        ++provider.Revision;Pixels(*viewport,view);Check(viewport->GetStatistics().InstanceBufferUploads==1&&provider.MeshLoads>loads,
            "Resource revision reused stale mesh or instance-buffer ownership");
        secondViewport=runtime->CreateOffscreenRenderer(256,256,&provider,error);Check(secondViewport,error);
        pixels=Pixels(*viewport,view);Equivalent(Pixels(*secondViewport,view),pixels,"Independent viewport instance ownership leaked");
        Check(secondViewport->GetStatistics().InstanceBufferUploads==1,"Second viewport reused a foreign GPU instance buffer");
        Pixels(*viewport,view);Check(viewport->GetStatistics().InstanceBufferUploads==0,"Other viewport invalidated resident instances");
        delete secondViewport;secondViewport=nullptr;
        provider.Materials[100].EnableGPUInstancing=false;++provider.Revision;
        Pixels(*viewport,view);Check(viewport->GetStatistics().DrawCalls==99&&viewport->GetStatistics().InstancedDrawCalls==0,
            "Material GPU instancing opt-out was ignored");
        provider.Materials[100].EnableGPUInstancing=true;++provider.Revision;

        // Exercise the four actual shader/input layouts, including reflected
        // winding and inverse-transpose normals under nonuniform scale.
        Check(runtime->DestroyScene(scene),"Cannot replace the bulk scene");provider.Normal={.57735027f,.57735027f,.57735027f};
        for(std::uint64_t layout=1;layout<=4;++layout){
            scene=runtime->CreateScene("Instance vertex layout "+std::to_string(layout));view=View(scene,6);
            provider.Materials[100].BaseColorTexture=layout>=3?ResourceReference{Guid,201}:ResourceReference{};++provider.Revision;
            for(int index=0;index<6;++index){auto* object=Object(*scene,layout,100,{(float(index%3)-1)*1.6f,index<3?-.9f:.9f,0});
                object->GetTransform()->SetLocalScale({index<3?1.25f:-1.25f,.45f,1.8f});
                object->GetTransform()->SetLocalRotation({0,0,.17364818f,.98480775f});}
            pixels=Pixels(*viewport,view);Check(viewport->GetStatistics().InstancedDrawCalls==2&&viewport->GetStatistics().InstancedInstances==6,
                "Optional layout or mirrored winding did not use separate compatible instanced pipelines");
            CompareSingleDraws(*viewport,view,pixels,"Optional attributes/nonuniform normals/mirrored transform changed the image");
            Check(runtime->DestroyScene(scene),"Cannot remove layout scene");
        }
        provider.Normal={0,0,1};provider.Materials[100].BaseColorTexture={};++provider.Revision;

        scene=runtime->CreateScene("Independent material batches");view=View(scene,6);
        auto* first=Object(*scene,4,100,{-1.5f,-1,0});Object(*scene,4,100,{-1.5f,1,0});
        Object(*scene,4,101,{1.5f,-1,0});Object(*scene,4,101,{1.5f,1,0});
        provider.Materials[100].BaseColorTexture={Guid,201};provider.Materials[101].BaseColorTexture={Guid,202};++provider.Revision;
        pixels=Pixels(*viewport,view);Check(viewport->GetStatistics().DrawCalls==2&&viewport->GetStatistics().InstancedInstances==4,
            "Distinct materials/textures were combined or compatible pairs were not batched");
        Pixels(*viewport,view);Check(viewport->GetStatistics().InstanceBufferUploads==0,"Static material groups were not resident");
        first->GetTransform()->SetLocalPosition({-1.25f,-1,0});Pixels(*viewport,view);
        Check(viewport->GetStatistics().InstanceBufferUploads==1,"One changed material group reuploaded other unchanged groups");
        pixels=Pixels(*viewport,view);CompareSingleDraws(*viewport,view,pixels,"Batched material or sampler state leaked between groups");
        Check(runtime->DestroyScene(scene),"Cannot replace material scene");

        scene=runtime->CreateScene("Submesh ranges");view=View(scene,6);
        Object(*scene,5,100,{-1,-1,0});Object(*scene,5,100,{1,1,0});
        pixels=Pixels(*viewport,view);Check(viewport->GetStatistics().DrawCalls==2&&viewport->GetStatistics().Triangles==4,
            "Different submesh index ranges or material slots were lost during grouping");
        CompareSingleDraws(*viewport,view,pixels,"Multi-section mesh rendered the wrong index range/material");
        Check(runtime->DestroyScene(scene),"Cannot replace section scene");

        // A batching sort must honor authoring priority even for coplanar opaque
        // surfaces: LessEqual depth means the last submitted material is visible.
        scene=runtime->CreateScene("Material sorting priority");view=View(scene,3);
        provider.Materials[100].BaseColorTexture={};provider.Materials[101].BaseColorTexture={};
        provider.Materials[100].SortingPriority=-10;provider.Materials[101].SortingPriority=10;++provider.Revision;
        for(unsigned index=0;index<2;++index){
            Object(*scene,1,100,{index?.6f:-.6f,0,0});Object(*scene,1,101,{index?.6f:-.6f,0,0});
        }
        pixels=Pixels(*viewport,view);Check(viewport->GetStatistics().DrawCalls==2,"Sorting priority disabled otherwise compatible batches");
        const auto green=Sample(pixels,viewport->GetStatistics(),-.6f,0,3);
        Check(green[1]>green[0],"Higher sorting priority was not drawn last");
        provider.Materials[100].SortingPriority=10;provider.Materials[101].SortingPriority=-10;++provider.Revision;
        pixels=Pixels(*viewport,view);const auto orange=Sample(pixels,viewport->GetStatistics(),-.6f,0,3);
        Check(orange[0]>orange[1],"Changing material sorting priority retained stale batch order");
        CompareSingleDraws(*viewport,view,pixels,"Batch regrouping changed material sorting priority");
        provider.Materials[100].SortingPriority=provider.Materials[101].SortingPriority=0;++provider.Revision;
        Check(runtime->DestroyScene(scene),"Cannot replace sorting scene");

        // Exercise the actual per-draw instance limit without a large framebuffer
        // fill cost; all tiny triangles deliberately overlap in a wide view.
        scene=runtime->CreateScene("Instance capacity boundary");view=View(scene,20);
        std::vector<IGameObject*> capacityObjects;capacityObjects.reserve(4100);
        for(unsigned index=0;index<4100;++index)capacityObjects.push_back(Object(*scene,1,100,{0,0,0}));
        pixels=Pixels(*viewport,view);stats=viewport->GetStatistics();
        Check(stats.DrawCalls==2&&stats.InstancedDrawCalls==2&&stats.InstancedInstances==4100&&stats.SavedDrawCalls==4098&&stats.Triangles==4100,
            "4100 instances did not split into correctly accounted bounded GPU draws");
        Check(Sample(pixels,stats,0,0,20)[0]>0,"Capacity-boundary instanced draw produced no pixels");
        Pixels(*viewport,view);Check(viewport->GetStatistics().InstanceBufferUploads==0,"Unchanged bounded instance chunks were reuploaded");
        for(unsigned index=0;index<4;++index){Check(scene->DestroyGameObject(capacityObjects.back()),"Cannot shrink capacity boundary");capacityObjects.pop_back();}
        const auto atCapacity=Pixels(*viewport,view);stats=viewport->GetStatistics();
        Check(stats.DrawCalls==1&&stats.InstancedInstances==4096&&stats.Triangles==4096,
            "Exactly 4096 instances overflowed or retained a stale second chunk");
        Equivalent(atCapacity,pixels,"Shrinking identical overlapping instances changed framebuffer output");
        Check(runtime->DestroyScene(scene),"Cannot replace capacity scene");

        scene=runtime->CreateScene("Layer-filtered light groups");view=View(scene,6);
        for(int index=0;index<4;++index){auto* object=Object(*scene,1,102,{index<2?-1.5f:1.5f,index%2?1.0f:-1.0f,0});
            Check(object->SetLayer(index<2?0:1),"Cannot set instanced light layer");}
        auto* red=scene->CreateGameObject("Red layer 0")->AddComponent<ILightComponent>();
        auto* blue=scene->CreateGameObject("Blue layer 1")->AddComponent<ILightComponent>();
        red->GetGameObject()->GetTransform()->SetLocalRotation({0,1,0,0});
        blue->GetGameObject()->GetTransform()->SetLocalRotation({0,1,0,0});
        Check(red->SetColor({1,0,0,1})&&red->SetCullingMask(1)&&blue->SetColor({0,0,1,1})&&blue->SetCullingMask(2),"Cannot configure filtered lights");
        pixels=Pixels(*viewport,view);Check(viewport->GetStatistics().DrawCalls==2&&viewport->GetStatistics().InstancedInstances==4,
            "Instances with different matching directional lights shared one lighting uniform");
        const auto left=Sample(pixels,viewport->GetStatistics(),-1.5f,-1,6),right=Sample(pixels,viewport->GetStatistics(),1.5f,-1,6);
        Check(left[0]>30&&left[2]==0&&right[2]>30&&right[0]==0,"Per-layer light radiance crossed batch boundaries");
        CompareSingleDraws(*viewport,view,pixels,"Layer-specific instanced Lit output differed from individual draws");
        Check(runtime->DestroyScene(scene),"Cannot replace light scene");

        scene=runtime->CreateScene("Specular material option");view=View(scene,4);
        Object(*scene,1,102,{-1,0,0});Object(*scene,1,102,{1,0,0});
        auto* sun=scene->CreateGameObject("Sun")->AddComponent<ILightComponent>();
        sun->GetGameObject()->GetTransform()->SetLocalRotation({0,1,0,0});
        provider.Materials[102].Metallic=1;provider.Materials[102].Roughness=.65f;provider.Materials[102].BaseColor={.5f,.5f,.5f,1};
        provider.Materials[102].SpecularHighlights=true;++provider.Revision;
        pixels=Pixels(*viewport,view);const auto highlight=Sample(pixels,viewport->GetStatistics(),-1,0,4);
        provider.Materials[102].SpecularHighlights=false;++provider.Revision;
        const auto noSpecular=Pixels(*viewport,view);const auto dark=Sample(noSpecular,viewport->GetStatistics(),-1,0,4);
        Check(highlight[0]>dark[0]+10&&dark[0]==0,"Specular Highlights switch did not disable metallic direct reflection");
        CompareSingleDraws(*viewport,view,noSpecular,"Specular opt-out was lost in instanced shader");
        Check(sun->SetEnabled(false),"Cannot disable specular test light");provider.Materials[102].Metallic=0;++provider.Revision;
        EnvironmentLightingSettings environment;environment.Mode=AmbientLightMode::Color;environment.AmbientColor={.25f,.125f,.0625f};
        Check(scene->SetEnvironmentLighting(environment),"Cannot set diffuse environment");const auto ambient=Pixels(*viewport,view);
        provider.Materials[102].SpecularHighlights=true;++provider.Revision;
        Equivalent(Pixels(*viewport,view),ambient,"Specular toggle incorrectly changed diffuse environment illumination");
        const auto ambientPixel=Sample(ambient,viewport->GetStatistics(),-1,0,4);
        Check(ambientPixel[0]>20&&ambientPixel[1]>10,"Ambient preservation comparison rendered no surface");

        delete viewport;viewport=nullptr;delete runtime;runtime=nullptr;
        std::cout<<"PASS actual GPU automatic instancing: 100 objects/one draw, full-image baseline, resident uploads, transforms/deletion/revision/viewports, optional layouts/mirrors/normals, material/section/light isolation, priority and 4096-capacity boundaries, opt-outs and specular/ambient\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';delete secondViewport;delete viewport;delete runtime;return 1;}
}
