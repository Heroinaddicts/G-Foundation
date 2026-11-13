#include "GameSproutRuntimeExport.h"
#include "RuntimeRenderApi.h"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace GameSproutRuntime;
namespace {
const char* Guid="0123456789abcdef0123456789abcdef";
void Check(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
class Provider final:public IRuntimeResourceProvider {
public:
    std::uint64_t Revision=1;
    unsigned MeshLoads=0,MaterialLoads=0;
    bool Green=false;
    float MeshOffset=0;
    std::uint64_t GetRevision() const override{return Revision;}
    bool LoadMesh(const ResourceReference& reference,MeshRenderData& data,std::string&) override {
        ++MeshLoads;data={};data.VertexCount=3;data.VertexStride=24;data.IndexCount=3;data.IndexOffset=72;
        const float offset=MeshOffset+(reference.LocalID==2?.7f:0);
        const float vertices[]={-.2f+offset,-.2f,0,0,0,1, .2f+offset,-.2f,0,0,0,1, offset,.2f,0,0,0,1};
        const std::uint32_t indices[]={0,1,2};data.Bytes.resize(sizeof(vertices)+sizeof(indices));
        std::memcpy(data.Bytes.data(),vertices,sizeof(vertices));std::memcpy(data.Bytes.data()+sizeof(vertices),indices,sizeof(indices));
        data.LocalBounds={{offset,0,0},{.2f,.2f,0}};return true;
    }
    bool LoadTexture(const ResourceReference&,TextureRenderData&,std::string&) override{return false;}
    bool LoadMaterial(const ResourceReference& reference,MaterialRenderData& data,std::string&) override {
        ++MaterialLoads;data={};
        if(reference.LocalID==11)data.BaseColor={1,0,0,1};
        else if(reference.LocalID==12)data.BaseColor=Green?Color{0,1,0,1}:Color{0,0,1,1};
        else {data.ShadingModel=MaterialShadingModel::Lit;data.Roughness=1;}
        return true;
    }
    bool LoadSkeleton(const ResourceReference&,SkeletonData& data,std::string&) override {
        data={};data.Nodes={{"/parent","Parent",-1,{}}};return true;
    }
    bool LoadAnimationClip(const ResourceReference&,AnimationClipData& data,std::string&) override {
        data={};data.Duration=1;data.SampleRate=1;data.Tracks={"/parent"};
        AnimationFrame first,last;last.Time=1;
        first.Poses.resize(1);last.Poses.resize(1);last.Poses[0].Position.X=.75f;
        data.Frames={first,last};return true;
    }
};
RenderView View(IScene* scene){
    RenderView view;view.Scene=scene;view.DrawGrid=false;view.ClearColor={0,0,0,1};view.Camera.Position={0,0,3};
    view.Camera.Projection.Projection=CameraProjection::Orthographic;view.Camera.Projection.OrthographicHeight=2;
    view.Camera.Projection.NearPlane=.1f;view.Camera.Projection.FarPlane=10;
    Check(TryMakeLookAt(view.Camera.Position,{0,0,0},{0,1,0},view.Camera.View),"Cannot aim snapshot camera");return view;
}
RenderStatistics Render(IRuntimeViewport& viewport,const RenderView& view){
    Check(viewport.Render(view),viewport.GetLastError());return viewport.GetStatistics();
}
std::array<int,3> Pixel(IRuntimeViewport& viewport,float worldX=0){
    std::vector<std::uint8_t> pixels;std::string error;Check(viewport.ReadPixels(pixels,error),error);
    const auto& stats=viewport.GetStatistics();
    const float extent=2.f*stats.Width/stats.Height;
    // The +Z-side Unity camera looks toward -Z; its local right is world -X.
    const auto x=std::uint32_t((.5f-worldX/extent)*stats.Width);
    Check(x<stats.Width,"Snapshot test sample outside target");
    const auto offset=(std::size_t(stats.Height/2)*stats.Width+x)*4;
    return {pixels[offset],pixels[offset+1],pixels[offset+2]};
}
IGameObject* Object(IScene& scene,IGameObject* parent=nullptr){
    auto* object=scene.CreateGameObject("Triangle",parent);Check(object,"Cannot create snapshot triangle");
    object->AddComponent<IMeshComponent>()->SetMesh({Guid,1});
    Check(object->AddComponent<IMeshRendererComponent>()->SetMaterials({{Guid,11}}),"Cannot assign snapshot material");return object;
}
void Rebuilt(const RenderStatistics& stats,const char* operation){
    Check(stats.SceneSnapshotRebuilds==1,std::string(operation)+": object snapshot was stale");
}
}
int main(int argc,char** argv){
    IGameSproutRuntime* runtime=nullptr;IRuntimeViewport* viewport=nullptr;IRuntimeViewport* second=nullptr;
    Provider provider;std::string error;
    try {
        Check(argc==2,"Pass Runtime binary directory");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        viewport=runtime->CreateOffscreenRenderer(128,128,&provider,error);
        if(!viewport){delete runtime;std::cout<<"SKIP GPU unavailable: "<<error<<'\n';return 77;}
        auto* scene=runtime->CreateScene("Render snapshots");auto* parent=scene->CreateGameObject("Parent");
        auto* object=Object(*scene,parent);auto view=View(scene);
        auto stats=Render(*viewport,view);
        Check(stats.SceneSnapshotRebuilds==1&&stats.LightSnapshotRebuilds==1&&stats.DrawCalls==1&&Pixel(*viewport)[0]>100,
            "First snapshot did not render the source state");
        const auto meshLoads=provider.MeshLoads,materialLoads=provider.MaterialLoads;
        stats=Render(*viewport,view);
        Check(!stats.SceneSnapshotRebuilds&&!stats.LightSnapshotRebuilds&&provider.MeshLoads==meshLoads&&provider.MaterialLoads==materialLoads,
            "Idle rendering rebuilt source snapshots or reloaded shared resources");
        Check(stats.VisibleObjects==1&&stats.DrawCalls==1&&Pixel(*viewport)[0]>100,"Reusing prepared draws lost statistics or source pixels");
        object->SetName("Renamed triangle");
        stats=Render(*viewport,view);
        Check(!stats.SceneSnapshotRebuilds&&!stats.LightSnapshotRebuilds&&Pixel(*viewport)[0]>100,
            "Author metadata invalidated render snapshots");
        auto clearOnly=view;clearOnly.DrawScene=false;clearOnly.ClearColor={0,0,1,1};
        stats=Render(*viewport,clearOnly);Check(stats.DrawCalls==0&&Pixel(*viewport)[2]>100,"Clear-only render reused stale prepared geometry");
        stats=Render(*viewport,view);Check(stats.DrawCalls==1&&Pixel(*viewport)[0]>100,"Clear-only frame damaged the retained scene preparation");
        auto* initialRenderer=object->GetComponent<IMeshRendererComponent>();
        Check(initialRenderer->SetMaterials({}),"Cannot select default material");
        Render(*viewport,view);stats=Render(*viewport,view);
        const auto white=Pixel(*viewport);
        Check(stats.DrawCalls==1&&white[0]>100&&white[1]>100&&white[2]>100,"Retained default-material draw borrowed a previous call's stack storage");
        Check(initialRenderer->SetMaterials({{Guid,11}}),"Cannot restore source material");Render(*viewport,view);

        // Camera movement and target extent change the frustum, not the scene.
        view.Camera.Position={3,0,3};Check(TryMakeLookAt(view.Camera.Position,{3,0,0},{0,1,0},view.Camera.View),"Cannot move snapshot camera");
        stats=Render(*viewport,view);Check(!stats.SceneSnapshotRebuilds&&stats.DrawCalls==0,"Camera movement retained old visibility or rebuilt scene data");
        view=View(scene);Render(*viewport,view);
        parent->GetTransform()->SetLocalPosition({.5f,0,0});
        parent->GetTransform()->SetLocalPosition({.75f,0,0});stats=Render(*viewport,view);
        Check(!stats.SceneSnapshotRebuilds&&!stats.LightSnapshotRebuilds&&
            provider.MeshLoads==meshLoads&&provider.MaterialLoads==materialLoads,
            "Transform-only edits rebuilt scene or light snapshots or reloaded resources");
        Check(stats.DrawCalls==1&&Pixel(*viewport,.75f)[0]>100&&Pixel(*viewport)[0]==0,
            "Descendant world transform or cached frustum bounds were stale");
        parent->GetTransform()->SetLocalPosition({5,0,0});stats=Render(*viewport,view);
        Check(!stats.SceneSnapshotRebuilds&&stats.DrawCalls==0,"Moving out of the same camera left a prepared draw visible");
        parent->GetTransform()->SetLocalPosition({.75f,0,0});stats=Render(*viewport,view);
        Check(!stats.SceneSnapshotRebuilds&&stats.DrawCalls==1,"Moving back into the same camera did not restore the prepared draw");
        Check(viewport->Resize(64,128,error),error);stats=Render(*viewport,view);
        Check(!stats.SceneSnapshotRebuilds&&stats.DrawCalls==0,"Resize did not recull cached world data at actual aspect");
        Check(viewport->Resize(128,128,error),error);parent->GetTransform()->SetLocalPosition({0,0,0});Render(*viewport,view);

        // An evaluated pose must update descendants without rebuilding scene membership.
        runtime->SetResourceProvider(&provider);
        auto* animator=scene->CreateGameObject("Animation source")->AddComponent<IAnimatorComponent>();
        animator->SetSkeleton({Guid,21});
        Check(animator->SetNodeBindings({{"/parent",parent->GetInstanceID()}}),"Cannot bind the mesh's parent to animation");
        Render(*viewport,view);
        Check(animator->PlayClip({Guid,22},1,1,false),animator->GetLastError());
        stats=Render(*viewport,view);
        Check(!stats.SceneSnapshotRebuilds&&!stats.LightSnapshotRebuilds&&Pixel(*viewport,.75f)[0]>100&&Pixel(*viewport)[0]==0,
            "Evaluated animation rebuilt the scene or left its mesh descendant stale");
        animator->StopClip();stats=Render(*viewport,view);
        Check(!stats.SceneSnapshotRebuilds&&!stats.LightSnapshotRebuilds&&Pixel(*viewport)[0]>100,
            "Stopping animation rebuilt the scene or left the evaluated pose in the snapshot");

        auto* body=parent->AddComponent<IRigidbodyComponent>();
        Check(body&&body->SetEnabled(true)&&body->SetGravityFactor(0),"Cannot initialize the mesh parent's Rigidbody");
        Check(runtime->BeginExecution(error),error);Render(*viewport,view);
        Check(body->Teleport({.75f,0,0},{}),"Cannot teleport the mesh parent's Rigidbody");
        runtime->FixedUpdate(1.f/60.f);stats=Render(*viewport,view);
        Check(!stats.SceneSnapshotRebuilds&&!stats.LightSnapshotRebuilds&&Pixel(*viewport,.75f)[0]>100&&Pixel(*viewport)[0]==0,
            "Physics pose rebuilt the scene or left its mesh descendant stale");
        Check(runtime->EndExecution(error),error);stats=Render(*viewport,view);
        Check(!stats.SceneSnapshotRebuilds&&!stats.LightSnapshotRebuilds&&Pixel(*viewport)[0]>100,
            "Stopping physics rebuilt the scene or left its simulated pose in the snapshot");
        Check(parent->RemoveComponent<IRigidbodyComponent>(),"Cannot remove the mesh parent's Rigidbody");
        Render(*viewport,view);

        parent->GetTransform()->SetLocalScale({0,1,1});stats=Render(*viewport,view);Rebuilt(stats,"Singular parent transform");
        Check(stats.DrawCalls==0&&stats.SkippedObjects==1,"Newly singular descendant remained in the snapshot");
        parent->GetTransform()->SetLocalScale({1,1,1});stats=Render(*viewport,view);Rebuilt(stats,"Recovered parent transform");
        Check(stats.DrawCalls==1&&Pixel(*viewport)[0]>100,"Recovered descendant was missing from the snapshot");
        view.Camera.Projection.CullingMask=2;stats=Render(*viewport,view);Rebuilt(stats,"Camera layer mask");Check(stats.DrawCalls==0,"Camera mask retained excluded objects");
        view.Camera.Projection.CullingMask=1;Render(*viewport,view);
        Check(object->SetLayer(1),"Cannot move object layer");stats=Render(*viewport,view);Rebuilt(stats,"Object layer");Check(stats.DrawCalls==0,"Object layer setter did not invalidate visibility");
        object->SetLayer(0);parent->SetActive(false);stats=Render(*viewport,view);Rebuilt(stats,"Parent active state");Check(stats.DrawCalls==0,"Inactive parent's cached child still rendered");
        parent->SetActive(true);stats=Render(*viewport,view);Check(stats.DrawCalls==1,"Reactivated child did not return");

        // No ProcessPendingEvents/Update between these setters and Render: the
        // cache must invalidate synchronously, independently of Editor dispatch.
        auto* mesh=object->GetComponent<IMeshComponent>();auto* renderer=object->GetComponent<IMeshRendererComponent>();
        mesh->SetMesh({Guid,2});stats=Render(*viewport,view);Rebuilt(stats,"Mesh binding");
        Check(Pixel(*viewport)[0]==0&&Pixel(*viewport,.7f)[0]>100,"Changed mesh binding kept cached geometry");
        mesh->SetMesh({Guid,1});renderer->SetMaterial(0,{Guid,12});stats=Render(*viewport,view);Rebuilt(stats,"Material binding");
        Check(Pixel(*viewport)[2]>100&&Pixel(*viewport)[0]==0,"Changed material binding kept cached material");
        provider.Green=true;++provider.Revision;stats=Render(*viewport,view);Rebuilt(stats,"Provider material revision");
        Check(Pixel(*viewport)[1]>100&&Pixel(*viewport)[2]==0,"Provider revision retained released/cached material pointers");
        provider.MeshOffset=.6f;++provider.Revision;stats=Render(*viewport,view);Rebuilt(stats,"Provider geometry revision");
        Check(Pixel(*viewport)[1]==0&&Pixel(*viewport,.6f)[1]>100,"Provider revision retained old geometry or derived bounds");
        provider.MeshOffset=0;++provider.Revision;Render(*viewport,view);
        renderer->SetVisible(false);stats=Render(*viewport,view);Rebuilt(stats,"Renderer visibility");Check(stats.DrawCalls==0,"Hidden renderer remained in snapshot");
        renderer->SetVisible(true);Render(*viewport,view);

        renderer->SetMaterial(0,{Guid,13});stats=Render(*viewport,view);Check(Pixel(*viewport)[0]==0,"Lit snapshot invented illumination");
        auto* sunParent=scene->CreateGameObject("Sun parent");
        auto* sunObject=scene->CreateGameObject("Sun",sunParent);sunObject->GetTransform()->SetLocalRotation({0,1,0,0});
        auto* sun=sunObject->AddComponent<ILightComponent>();
        stats=Render(*viewport,view);Check(stats.LightSnapshotRebuilds==1&&Pixel(*viewport)[0]>50,"New directional light was absent from cached lighting");
        Check(sun->SetColor({0,0,1,1}),"Cannot change snapshot light");stats=Render(*viewport,view);
        Check(stats.LightSnapshotRebuilds==1&&Pixel(*viewport)[2]>50&&Pixel(*viewport)[0]==0,"Light setter did not update the next render");
        sunParent->GetTransform()->SetLocalRotation({0,1,0,0});stats=Render(*viewport,view);
        Check(!stats.SceneSnapshotRebuilds&&stats.LightSnapshotRebuilds==1&&Pixel(*viewport)[2]==0,
            "Transforming a light's ancestor left stale lighting or rebuilt unrelated mesh data");
        sunParent->GetTransform()->SetLocalRotation({});stats=Render(*viewport,view);
        Check(stats.LightSnapshotRebuilds==1&&Pixel(*viewport)[2]>50,"Restoring light ancestry did not restore its direction");
        sunObject->SetActive(false);stats=Render(*viewport,view);Check(Pixel(*viewport)[2]==0,"Inactive light retained cached radiance");
        EnvironmentLightingSettings environment;environment.Mode=AmbientLightMode::Color;environment.AmbientColor={.1f,.2f,.3f};
        Check(scene->SetEnvironmentLighting(environment),"Cannot set snapshot environment");stats=Render(*viewport,view);
        Check(Pixel(*viewport)[2]>Pixel(*viewport)[1]&&Pixel(*viewport)[1]>20,"Environment setter did not affect cached objects");
        stats=Render(*viewport,view);Check(!stats.SceneSnapshotRebuilds&&!stats.LightSnapshotRebuilds,"Idle lighting rebuilt its snapshots");

        // A separate viewport owns its own snapshots/resources; rendering it
        // must not invalidate this viewport or borrow another device's buffers.
        second=runtime->CreateOffscreenRenderer(128,128,&provider,error);Check(second,error);
        stats=Render(*second,view);Check(stats.SceneSnapshotRebuilds==1&&stats.LightSnapshotRebuilds==1,"New viewport inherited another viewport's snapshot flags");
        stats=Render(*viewport,view);Check(!stats.SceneSnapshotRebuilds&&!stats.LightSnapshotRebuilds,"Second viewport invalidated the first viewport");
        parent->GetTransform()->SetLocalPosition({.65f,0,0});
        stats=Render(*second,view);Check(!stats.SceneSnapshotRebuilds&&Pixel(*second,.65f)[2]>20,"First reader lost incremental transforms");
        stats=Render(*viewport,view);Check(!stats.SceneSnapshotRebuilds&&Pixel(*viewport,.65f)[2]>20,"One viewport consumed another viewport's transform changes");
        parent->GetTransform()->SetLocalPosition({0,0,0});
        delete second;second=nullptr;

        renderer->SetMaterial(0,{Guid,12});Check(scene->AddChild(object,false),"Cannot reparent snapshot object");
        parent->GetTransform()->SetLocalPosition({5,0,0});stats=Render(*viewport,view);Rebuilt(stats,"Reparent");
        Check(stats.DrawCalls==1&&Pixel(*viewport)[1]>100,"Reparent retained old ancestor transform");
        auto* other=runtime->CreateScene("Second scene");auto otherView=View(other);
        Check(!other->AddChild(object,false),"Direct cross-scene attach violated the hierarchy contract");
        Check(object->SetParent(nullptr,false)&&other->AddChild(object,false),"Cannot move a detached runtime object to another scene");
        stats=Render(*viewport,view);Rebuilt(stats,"Detach from previous scene");Check(stats.DrawCalls==0,"Old scene snapshot retained a detached object");
        stats=Render(*viewport,otherView);Rebuilt(stats,"Switch scenes");Check(stats.DrawCalls==1&&Pixel(*viewport)[1]>100,"New scene did not render attached object");
        stats=Render(*viewport,view);Rebuilt(stats,"Return to previous scene");Check(stats.DrawCalls==0,"Returning to a scene reused another scene's objects");
        Check(object->SetParent(nullptr,false)&&scene->AddChild(object,false),"Cannot restore runtime object");
        Render(*viewport,view);Check(scene->DestroyGameObject(object),"Cannot delete cached object");object=nullptr;
        stats=Render(*viewport,view);Rebuilt(stats,"Destroy");Check(stats.DrawCalls==0,"Destroyed object's cached data was dereferenced or drawn");
        object=Object(*scene);stats=Render(*viewport,view);Rebuilt(stats,"Create");Check(stats.DrawCalls==1&&Pixel(*viewport)[0]>100,"Newly created object was omitted");
        Check(object->RemoveComponent<IMeshRendererComponent>(),"Cannot remove cached renderer");stats=Render(*viewport,view);Rebuilt(stats,"Remove renderer");
        Check(stats.DrawCalls==0,"Removed renderer still produced cached draws");
        object->AddComponent<IMeshRendererComponent>();Render(*viewport,view);
        Check(object->RemoveComponent<IMeshComponent>(),"Cannot remove cached mesh");stats=Render(*viewport,view);Rebuilt(stats,"Remove mesh");Check(stats.DrawCalls==0,"Removed mesh still produced cached draws");

        delete viewport;viewport=nullptr;delete runtime;runtime=nullptr;
        std::cout<<"PASS render snapshots: idle/camera/resize reuse, animation and physics poses, synchronous setters, hierarchy/layer/visibility, resource revisions, lights/environment, scene transfers, component removal and viewport isolation\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';delete second;delete viewport;delete runtime;return 1;}
}
