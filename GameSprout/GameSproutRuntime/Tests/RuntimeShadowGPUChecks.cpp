#include "GameSproutRuntimeExport.h"
#include "RuntimeRenderApi.h"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace GameSproutRuntime;

namespace {
constexpr const char* Guid="0123456789abcdef0123456789abcdef";
void Check(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
void UInt(std::vector<std::uint8_t>& bytes,std::uint32_t value){
    for(unsigned shift=0;shift<32;shift+=8)bytes.push_back(std::uint8_t(value>>shift));
}
void Float(std::vector<std::uint8_t>& bytes,float value){
    std::uint32_t bits;std::memcpy(&bits,&value,sizeof(bits));UInt(bytes,bits);
}
class Provider final:public IRuntimeResourceProvider {
public:
    std::uint64_t Revision=1;
    float CubeHalfSize=.4f;
    std::uint64_t GetRevision()const override{return Revision;}
    bool LoadMesh(const ResourceReference& reference,MeshRenderData& data,std::string& error)override{
        data={};error.clear();data.VertexStride=24;
        std::vector<Vector3> positions;std::vector<std::uint32_t> indices;
        if(reference.LocalID==1){
            // A camera-facing receiving plane. It fills the entire test target,
            // so background/sky changes cannot be mistaken for a shadow.
            positions={{-4,-4,0},{4,-4,0},{4,4,0},{-4,4,0}};indices={0,1,2,0,2,3};
            data.LocalBounds={{},{4,4,0}};
        }else{
            const float h=CubeHalfSize;
            positions={{-h,-h,-h},{h,-h,-h},{h,h,-h},{-h,h,-h},
                {-h,-h,h},{h,-h,h},{h,h,h},{-h,h,h}};
            indices={4,5,6,4,6,7,1,0,3,1,3,2,5,1,2,5,2,6,
                0,4,7,0,7,3,7,6,2,7,2,3,0,1,5,0,5,4};
            data.LocalBounds={{},{h,h,h}};
        }
        data.VertexCount=static_cast<std::uint32_t>(positions.size());
        for(const auto position:positions){
            Float(data.Bytes,position.X);Float(data.Bytes,position.Y);Float(data.Bytes,position.Z);
            Float(data.Bytes,0);Float(data.Bytes,0);Float(data.Bytes,1);
        }
        data.IndexOffset=data.Bytes.size();data.IndexCount=static_cast<std::uint32_t>(indices.size());
        for(const auto index:indices)UInt(data.Bytes,index);
        return true;
    }
    bool LoadTexture(const ResourceReference&,TextureRenderData& data,std::string& error)override{
        data={};data.Width=data.Height=1;data.Pixels={255,255,255,255};error.clear();return true;
    }
    bool LoadMaterial(const ResourceReference&,MaterialRenderData& data,std::string& error)override{
        data={};data.ShadingModel=MaterialShadingModel::Lit;data.BaseColor={.4f,.4f,.4f,1};
        data.Roughness=1;data.SpecularHighlights=false;error.clear();return true;
    }
};
struct Frame {RenderStatistics Statistics;std::vector<std::uint8_t> Pixels;};
Frame Render(IRuntimeViewport& viewport,const RenderView& view){
    Check(viewport.Render(view),viewport.GetLastError());Frame frame;frame.Statistics=viewport.GetStatistics();
    std::string error;Check(viewport.ReadPixels(frame.Pixels,error),error);
    Check(frame.Pixels.size()==std::size_t(frame.Statistics.Width)*frame.Statistics.Height*4,"Unexpected shadow readback size");
    return frame;
}
unsigned Difference(const Frame& a,const Frame& b,int minimum=3){
    Check(a.Pixels.size()==b.Pixels.size(),"Different shadow target sizes");unsigned count=0;
    for(std::size_t offset=0;offset<a.Pixels.size();offset+=4){
        int difference=0;for(unsigned channel=0;channel<3;++channel)
            difference+=std::abs(int(a.Pixels[offset+channel])-int(b.Pixels[offset+channel]));
        if(difference>minimum)++count;
    }
    return count;
}
struct DarkRegion {unsigned Count=0;double CenterX=0;};
DarkRegion Darkened(const Frame& unshadowed,const Frame& shadowed){
    Check(unshadowed.Pixels.size()==shadowed.Pixels.size(),"Different shadow target sizes");DarkRegion result;
    for(std::size_t offset=0;offset<unshadowed.Pixels.size();offset+=4){
        int reference=0,actual=0;
        for(unsigned channel=0;channel<3;++channel){reference+=unshadowed.Pixels[offset+channel];actual+=shadowed.Pixels[offset+channel];}
        if(reference>30&&reference-actual>18){++result.Count;result.CenterX+=(offset/4)%unshadowed.Statistics.Width;}
    }
    if(result.Count)result.CenterX/=result.Count;return result;
}
void EffectiveShadow(const Frame& baseline,const Frame& actual,const char* operation){
    const auto dark=Darkened(baseline,actual);
    Check(dark.Count>100&&dark.Count<baseline.Statistics.Width*baseline.Statistics.Height/3,
        std::string(operation)+": expected a bounded, visibly darker shadow rather than no shadow/full-plane acne; dark pixels="+std::to_string(dark.Count));
}
IGameObject* MeshObject(IScene& scene,const char* name,std::uint64_t mesh){
    auto* object=scene.CreateGameObject(name);Check(object,"Cannot create shadow mesh object");
    auto* component=object->AddComponent<IMeshComponent>();Check(component,"Cannot create shadow mesh component");
    component->SetMesh({Guid,mesh});
    Check(object->AddComponent<IMeshRendererComponent>()->SetMaterials({{Guid,10}}),"Cannot bind shadow material");
    return object;
}
Quaternion Yaw(float degrees){const float half=DegreesToRadians(degrees)*.5f;return {0,std::sin(half),0,std::cos(half)};}

void CheckScrollingShadows(IGameSproutRuntime& runtime,IRuntimeViewport& viewport,Provider& provider) {
    std::string error;
    Check(viewport.Resize(256,256,error),error);
    provider.CubeHalfSize=.28f;
    ++provider.Revision;
    auto* scene=runtime.CreateScene("Scrolling directional shadow GPU checks");
    Check(scene,"Cannot create scrolling shadow scene");
    auto* receiver=MeshObject(*scene,"Scrolling shadow receiver",1);
    receiver->GetTransform()->SetLocalScale({5,5,1});
    Check(receiver->SetStaticFlags(StaticFlags::ShadowCasterStatic),"Cannot mark scrolling receiver static");
    for(int y=-3;y<=9;++y)for(int x=-3;x<=9;++x) {
        auto* caster=MeshObject(*scene,"Shadow scroll sample caster",2);
        caster->GetTransform()->SetLocalPosition({x*1.05f+.27f,y*1.05f+.14f,.75f});
        Check(caster->SetStaticFlags(StaticFlags::ShadowCasterStatic),"Cannot mark scrolling caster static");
    }
    auto* edge=MeshObject(*scene,"Caster in entering shadow strip",2);
    edge->GetTransform()->SetLocalPosition({4.33f,.5f,.7f});
    Check(edge->SetStaticFlags(StaticFlags::ShadowCasterStatic),"Cannot mark entering caster static");
    // Identical caster depth extrema remain in every tile along this path.
    // This exercises scrolling rather than the changed-depth fallback.
    for(float diagonal:{0.f,3.f,6.f,9.f})for(float z:{-5.f,5.f}) {
        auto* anchor=MeshObject(*scene,"Scroll depth anchor",2);
        anchor->GetTransform()->SetLocalPosition({diagonal,diagonal,z});
        Check(anchor->SetStaticFlags(StaticFlags::ShadowCasterStatic),"Cannot mark depth anchor static");
    }
    auto* sunObject=scene->CreateGameObject("Scroll sun");
    Check(sunObject,"Cannot create scroll light object");
    auto* sun=sunObject->AddComponent<ILightComponent>();
    Check(sun&&sunObject->GetTransform()->SetLocalRotation({0,1,0,0}),"Cannot aim scroll light");
    auto settings=sun->GetSettings();
    settings.Intensity=.5f;
    settings.Shadows.Enabled=false;
    settings.Shadows.CacheStaticCasters=true;
    settings.Shadows.Resolution=256;
    settings.Shadows.CascadeCount=1;
    settings.Shadows.Distance=3.5f;
    settings.Shadows.NormalBias=0;
    settings.Shadows.DepthBias=1;
    settings.Shadows.Soft=true;
    Check(sun->SetSettings(settings),"Cannot configure scroll lighting");

    RenderView view;
    view.Scene=scene;
    view.DrawGrid=false;
    view.EnableGPUInstancing=false;
    view.Camera.Projection.Projection=CameraProjection::Orthographic;
    view.Camera.Projection.OrthographicHeight=4;
    view.Camera.Projection.NearPlane=.5f;
    view.Camera.Projection.FarPlane=4;
    Mat4 baseView;
    Check(TryMakeLookAt({0,0,3},{.6f,0,0},{0,1,0},baseView),"Cannot aim scroll camera");
    const auto move=[&](float x,float y) {
        view.Camera.Position={x,y,3};
        view.Camera.View=baseView;
        for(std::size_t row=0;row<3;++row)
            view.Camera.View(row,3)-=baseView(row,0)*x+baseView(row,1)*y;
    };
    move(0,0);
    const auto unshadowed=Render(viewport,view);
    settings.Shadows.Enabled=true;
    Check(sun->SetSettings(settings),"Cannot enable scroll shadows");
    const auto initial=Render(viewport,view);
    Check(initial.Statistics.StaticShadowDrawCalls>20,"Scrolling fixture did not populate a static shadow map");
    EffectiveShadow(unshadowed,initial,"Initial scrolling shadow atlas");
    unsigned scrollCount=0;
    for(const Vector2 offset: {Vector2{1.5f,1.5f},Vector2{3,3},Vector2{4.5f,4.5f},
        Vector2{6,6},Vector2{7.5f,7.5f},Vector2{6,6},Vector2{4.5f,4.5f},Vector2{3,3},Vector2{1.5f,1.5f},Vector2{0,0}}) {
        move(offset.X,offset.Y);
        const auto scrolled=Render(viewport,view);
        struct OwnedViewport { IRuntimeViewport* Pointer; ~OwnedViewport(){delete Pointer;} };
        const OwnedViewport fresh{runtime.CreateOffscreenRenderer(256,256,&provider,error)};
        Check(fresh.Pointer,error);
        const auto rebuilt=Render(*fresh.Pointer,view);
        if(scrolled.Statistics.StaticShadowCascadesUpdated==1&&
            scrolled.Statistics.StaticShadowDrawCalls<rebuilt.Statistics.StaticShadowDrawCalls)++scrollCount;
        Check(Difference(scrolled,rebuilt,8)<100,
            "Scrolled shadow atlas changed overlapping or newly exposed shadow pixels");
    }
    Check(scrollCount>=6,"Camera motion did not reuse static depth across repeated signed texel wraps");
}

void CheckLayeredShadows(IGameSproutRuntime& runtime,IRuntimeViewport& viewport,Provider& provider) {
    std::string error;
    Check(viewport.Resize(256,256,error),error);
    provider.CubeHalfSize=.4f;
    ++provider.Revision;
    auto* scene=runtime.CreateScene("Layered directional shadow GPU checks");
    Check(scene,"Cannot create the layered shadow scene");
    auto* receiver=MeshObject(*scene,"Static receiving plane",1);
    auto* building=MeshObject(*scene,"Static casting cube",2);
    auto* character=MeshObject(*scene,"Dynamic casting cube",2);
    Check(receiver->SetStaticFlags(StaticFlags::ShadowCasterStatic)&&building->SetStaticFlags(StaticFlags::ShadowCasterStatic),
        "Cannot mark static shadow casters");
    // Only the receiver is visible to the camera. The two silhouettes fall on
    // different parts of it, so moving/removing a caster changes only shadows.
    building->GetTransform()->SetLocalPosition({2.7f,.9f,2});
    character->GetTransform()->SetLocalPosition({2.7f,-.65f,2});
    auto* sunObject=scene->CreateGameObject("Layered sun");
    Check(sunObject,"Cannot create layered light object");
    auto* sun=sunObject->AddComponent<ILightComponent>();
    Check(sun&&sunObject->GetTransform()->SetLocalRotation(Yaw(225)),"Cannot create/aim layered shadow light");
    auto settings=sun->GetSettings();
    settings.Intensity=.5f;
    settings.Shadows.Enabled=false;
    settings.Shadows.Resolution=512;
    settings.Shadows.Distance=20;
    settings.Shadows.CascadeCount=1;
    settings.Shadows.Strength=1;
    settings.Shadows.DepthBias=.5f;
    settings.Shadows.NormalBias=1;
    settings.Shadows.Soft=true;
    Check(sun->SetSettings(settings),"Cannot configure layered shadow fixture");
    EnvironmentLightingSettings environment;
    environment.Mode=AmbientLightMode::Color;
    environment.AmbientColor={.04f,.04f,.04f,1};
    Check(scene->SetEnvironmentLighting(environment),"Cannot set layered shadow-test ambient");

    RenderView view;
    view.Scene=scene;
    view.DrawGrid=false;
    view.Camera.Position={0,0,5};
    view.Camera.Projection.Projection=CameraProjection::Orthographic;
    view.Camera.Projection.OrthographicHeight=4;
    view.Camera.Projection.NearPlane=.1f;
    view.Camera.Projection.FarPlane=30;
    Check(TryMakeLookAt(view.Camera.Position,{0,0,0},{0,1,0},view.Camera.View),"Cannot aim layered shadow camera");
    const auto unshadowed=Render(viewport,view);
    Check(unshadowed.Statistics.VisibleObjects==1,"Layered shadow fixture must show only the receiver");
    settings.Shadows.Enabled=true;
    Check(sun->SetSettings(settings),"Cannot enable unified shadows");
    const auto unified=Render(viewport,view);
    EffectiveShadow(unshadowed,unified,"Unified reference for layered PCF");
    Check(unified.Statistics.ShadowMapUpdates==1&&unified.Statistics.StaticShadowMapUpdates==0&&
        unified.Statistics.DynamicShadowMapUpdates==0,"Unified shadows unexpectedly used the split atlases");

    settings.Shadows.CacheStaticCasters=true;
    Check(sun->SetSettings(settings),"Cannot enable static caster caching");
    const auto layered=Render(viewport,view);
    Check(layered.Statistics.StaticShadowMapUpdates==1&&layered.Statistics.DynamicShadowMapUpdates==1&&
        layered.Statistics.StaticShadowDrawCalls>0&&layered.Statistics.DynamicShadowDrawCalls>0,
        "Layered shadows must initialize both populated atlases");
    EffectiveShadow(unshadowed,layered,"Combined static/dynamic PCF");
    Check(Darkened(unified,layered).Count<100&&Darkened(layered,unified).Count<100,
        "Combined PCF lost or introduced a significant shadow compared with unified rendering");

    // A Canvas insert emits Create/LayerChanged before its UI components are
    // installed. These events must preserve the 3D extraction and shadow fit.
    auto* canvas=scene->CreateGameObject("Canvas");
    Check(canvas&&canvas->SetLayer(UILayer)&&canvas->AddComponent<IRectTransformComponent>(),
        "Cannot create UI shadow-cache fixture");
    auto* canvasComponent=canvas->AddComponent<ICanvasComponent>();
    Check(canvasComponent&&canvasComponent->SetReferenceResolution({256,256}),"Cannot configure fixture Canvas");
    auto* panel=scene->CreateGameObject("Panel",canvas);
    Check(panel&&panel->SetLayer(UILayer)&&panel->AddComponent<IRectTransformComponent>(),
        "Cannot lay out fixture panel");
    auto* image=panel->AddComponent<IImageComponent>();
    Check(image,"Cannot create fixture panel image");
    image->SetColor({1,0,0,1});
    view.DrawUI=true;
    const auto withCanvas=Render(viewport,view);
    Check(withCanvas.Statistics.UIDrawCalls==1&&Difference(layered,withCanvas)>100,
        "UI insertion was absent from the next overlay frame");
    Check(!withCanvas.Statistics.SceneSnapshotRebuilds&&!withCanvas.Statistics.LightSnapshotRebuilds&&
        !withCanvas.Statistics.ShadowMapUpdates&&!withCanvas.Statistics.ShadowDrawCalls,
        "Pure UI insertion rebuilt mesh/light snapshots or shadows");
    canvas->SetActive(false);
    const auto hiddenCanvas=Render(viewport,view);
    Check(!hiddenCanvas.Statistics.UIDrawCalls&&!hiddenCanvas.Statistics.SceneSnapshotRebuilds&&
        !hiddenCanvas.Statistics.LightSnapshotRebuilds&&!hiddenCanvas.Statistics.ShadowMapUpdates&&
        Difference(layered,hiddenCanvas)==0,"Hiding a UI-only subtree changed the world render");
    Check(scene->DestroyGameObject(canvas),"Cannot delete fixture Canvas");
    const auto removedCanvas=Render(viewport,view);
    Check(!removedCanvas.Statistics.SceneSnapshotRebuilds&&!removedCanvas.Statistics.LightSnapshotRebuilds&&
        !removedCanvas.Statistics.ShadowMapUpdates&&Difference(layered,removedCanvas)==0,
        "Deleting a UI-only subtree invalidated cached world geometry or shadows");
    view.DrawUI=false;

    character->GetTransform()->SetLocalPosition({2.7f,-1.35f,2});
    const auto dynamicMoved=Render(viewport,view);
    Check(dynamicMoved.Statistics.StaticShadowMapUpdates==0&&dynamicMoved.Statistics.StaticShadowDrawCalls==0&&
        dynamicMoved.Statistics.DynamicShadowMapUpdates==1&&dynamicMoved.Statistics.DynamicShadowDrawCalls>0,
        "Moving a dynamic caster should update only the dynamic atlas");
    Check(Darkened(layered,dynamicMoved).Count>100&&Darkened(dynamicMoved,layered).Count>100,
        "Moving the dynamic caster did not add its new shadow and clear its old shadow");

    const auto fixedCamera=view.Camera;
    view.Camera.Position={.2f,0,5};
    Check(TryMakeLookAt(view.Camera.Position,{.2f,0,0},{0,1,0},view.Camera.View),"Cannot shift covered shadow camera");
    character->GetTransform()->SetLocalPosition({2.7f,-.85f,2});
    const auto covered=Render(viewport,view);
    Check(covered.Statistics.StaticShadowMapUpdates==0&&covered.Statistics.StaticShadowDrawCalls==0&&
        covered.Statistics.DynamicShadowMapUpdates==1&&covered.Statistics.DynamicShadowDrawCalls>0,
        "Covered gameplay camera motion redrew the static map or omitted dynamic depth");
    view.Camera.Position={40,0,5};
    Check(TryMakeLookAt(view.Camera.Position,{40,0,0},{0,1,0},view.Camera.View),"Cannot move camera out of shadow coverage");
    Check(Render(viewport,view).Statistics.StaticShadowMapUpdates==1,"Camera teleport froze the old static map");
    view.Camera=fixedCamera;
    Check(Render(viewport,view).Statistics.StaticShadowMapUpdates==1,"Returning from a teleport did not restore static shadows");

    view.Camera.Position={.25f,0,5};
    Check(TryMakeLookAt(view.Camera.Position,{.25f,0,0},{0,1,0},view.Camera.View),"Cannot move layered shadow camera");
    view.ReuseShadowsWhileNavigating=true;
    character->GetTransform()->SetLocalPosition({2.7f,-.65f,2});
    const auto navigating=Render(viewport,view);
    Check(navigating.Statistics.StaticShadowMapUpdates==0&&navigating.Statistics.DynamicShadowMapUpdates==1,
        "Navigation should reuse the static fit and keep moving casters current");
    view.ReuseShadowsWhileNavigating=false;
    const auto settled=Render(viewport,view);
    Check(settled.Statistics.StaticShadowMapUpdates==1&&settled.Statistics.DynamicShadowMapUpdates==1,
        "Ending navigation should refit both shadow layers to the final camera");

    view.Camera=fixedCamera;
    const auto beforeStaticMove=Render(viewport,view);
    building->GetTransform()->SetLocalPosition({2.7f,1.35f,2});
    const auto staticMoved=Render(viewport,view);
    Check(staticMoved.Statistics.StaticShadowMapUpdates==1&&staticMoved.Statistics.StaticShadowDrawCalls>0&&
        Difference(beforeStaticMove,staticMoved)>100,"Editing a static caster left stale cached depth");

    // Moving along the light ray leaves the footprint on the receiver fixed,
    // but puts the dynamic cube far outside the static atlas's light depth range.
    character->GetTransform()->SetLocalPosition({42.7f,-.65f,42});
    const auto distantDynamic=Render(viewport,view);
    Check(distantDynamic.Statistics.StaticShadowMapUpdates==0&&distantDynamic.Statistics.DynamicShadowDrawCalls>0,
        "A distant dynamic caster forced a static depth refit or was clipped before drawing");
    Check(Darkened(staticMoved,distantDynamic).Count<100&&Darkened(distantDynamic,staticMoved).Count<100,
        "The dynamic shadow shifted when the caster moved along the light ray");

    settings.Shadows.CacheStaticCasters=false;
    Check(sun->SetSettings(settings),"Cannot select unified shadows for distant caster reference");
    const auto distantUnified=Render(viewport,view);
    Check(Darkened(distantUnified,distantDynamic).Count<100&&Darkened(distantDynamic,distantUnified).Count<100,
        "Combined shadow sampling lost the distant dynamic caster under a different depth projection");
    settings.Shadows.CacheStaticCasters=true;
    Check(sun->SetSettings(settings),"Cannot restore layered shadows after the distant caster reference");
    Render(viewport,view);

    Check(scene->DestroyGameObject(character),"Cannot remove the dynamic shadow caster");
    const auto staticOnly=Render(viewport,view);
    Check(staticOnly.Statistics.StaticShadowMapUpdates==0&&staticOnly.Statistics.DynamicShadowDrawCalls==0,
        "Removing the dynamic caster should keep the static atlas and draw no dynamic geometry");
    Check(Darkened(staticOnly,distantDynamic).Count>100,"The distant dynamic shadow was missing or stayed after caster removal");
    EffectiveShadow(unshadowed,staticOnly,"Remaining static shadow");

    settings.Shadows.CacheStaticCasters=false;
    Check(sun->SetSettings(settings),"Cannot restore unified shadows");
    const auto restored=Render(viewport,view);
    Check(restored.Statistics.ShadowMapUpdates==1&&restored.Statistics.StaticShadowMapUpdates==0&&
        restored.Statistics.DynamicShadowMapUpdates==0&&Darkened(staticOnly,restored).Count<100&&
        Darkened(restored,staticOnly).Count<100,"Disabling layered shadows left split atlas state in the unified pass");

    auto* group=scene->CreateGameObject("Edited parent");
    Check(group&&building->SetParent(group),"Cannot group the static shadow caster");
    for(bool configuredLayers:{false,true}) {
        settings.Shadows.CacheStaticCasters=configuredLayers;
        Check(sun->SetSettings(settings),"Cannot set authored layer policy for preview");
        view.TransformPreviewObjectID=group->GetInstanceID();
        const auto started=Render(viewport,view);
        Check(started.Statistics.StaticShadowMapUpdates==1&&started.Statistics.DynamicShadowDrawCalls>0,
            "Interactive parent did not move its static descendant into the dynamic layer");
        auto* transform=group->GetTransform();
        transform->SetLocalPosition(transform->GetLocalPosition()+Vector3{0,-.55f,0});
        const auto moved=Render(viewport,view);
        Check(moved.Statistics.StaticShadowMapUpdates==0&&moved.Statistics.DynamicShadowMapUpdates==1&&
            Darkened(started,moved).Count>100&&Darkened(moved,started).Count>100,
            "Drag preview retained stale shadows or redrew the cached surroundings");
        view.TransformPreviewObjectID=0;
        const auto committed=Render(viewport,view);
        Check(committed.Statistics.ShadowMapUpdates==0&&
            Darkened(committed,moved).Count<100&&Darkened(moved,committed).Count<100,
            "Ending the preview did not retain the exact submitted shadow silhouette");
        transform->SetLocalPosition(transform->GetLocalPosition()+Vector3{0,.1f,0});
        const auto invalidated=Render(viewport,view);
        Check(invalidated.Statistics.LayeredShadows==configuredLayers&&invalidated.Statistics.ShadowMapUpdates==1,
            "The next scene change did not restore the authored shadow policy");
    }

    // Start with a unified atlas. The selected caster moves before the first
    // preview frame, so the repair must use its previously submitted bounds.
    settings.Shadows.CacheStaticCasters=false;
    Check(sun->SetSettings(settings),"Cannot warm the unified repair reference");
    auto* groupTransform=group->GetTransform();
    groupTransform->SetLocalPosition({});
    building->GetTransform()->SetLocalPosition({2.7f,.1f,2});
    auto* crossing=MeshObject(*scene,"Overlapping static shadow",2);
    crossing->GetTransform()->SetLocalPosition({2.7f,.5f,2});
    auto* other=MeshObject(*scene,"Second selected caster",2);
    other->GetTransform()->SetLocalPosition({2.7f,1.45f,2});
    std::vector<IGameObject*> background{crossing,other};
    for(float y:{-6.f,-5.f,-4.f,4.f,5.f,6.f}) {
        auto* unchanged=MeshObject(*scene,"Unchanged caster outside the repair",2);
        unchanged->GetTransform()->SetLocalPosition({2.7f,y,2});
        background.push_back(unchanged);
    }
    view.EnableGPUInstancing=false;
    const auto warm=Render(viewport,view);
    const auto checkRepair=[&](const Frame& patched,const char* message) {
        struct OwnedViewport { IRuntimeViewport* Pointer; ~OwnedViewport(){delete Pointer;} };
        const OwnedViewport fresh{runtime.CreateOffscreenRenderer(256,256,&provider,error)};
        Check(fresh.Pointer,error);
        const auto rebuilt=Render(*fresh.Pointer,view);
        Check(patched.Statistics.StaticShadowMapUpdates==1&&patched.Statistics.DynamicShadowMapUpdates==1&&
            patched.Statistics.StaticShadowDrawCalls<rebuilt.Statistics.StaticShadowDrawCalls,
            std::string(message)+": repair redrew the whole static atlas");
        Check(Difference(patched,rebuilt,8)<100,std::string(message)+": repair changed the rebuilt shadow silhouette");
    };
    view.TransformPreviewObjectID=group->GetInstanceID();
    groupTransform->SetLocalPosition({0,-.95f,0});
    const auto firstMotion=Render(viewport,view);
    Check(Darkened(warm,firstMotion).Count>100&&Darkened(firstMotion,warm).Count>100,
        "First motion must replace the old caster footprint immediately");
    checkRepair(firstMotion,"First motion from unified atlas");

    view.TransformPreviewObjectID=0;
    Check(Render(viewport,view).Statistics.ShadowMapUpdates==0,"Selection release invalidated unchanged depth");
    view.TransformPreviewObjectID=other->GetInstanceID();
    other->GetTransform()->SetLocalPosition({2.7f,1.9f,2});
    checkRepair(Render(viewport,view),"Changing the preview caster");
    view.TransformPreviewObjectID=0;
    view.EnableGPUInstancing=true;
    for(auto* object:background)Check(scene->DestroyGameObject(object),"Cannot remove repair fixture caster");

    Check(receiver->SetParent(group),"Cannot group the final static shadow candidate");
    settings.Shadows.CacheStaticCasters=false;
    Check(sun->SetSettings(settings),"Cannot restore unified shadows before previewing every caster");
    Render(viewport,view);
    view.TransformPreviewObjectID=group->GetInstanceID();
    const auto allMoving=Render(viewport,view);
    Check(allMoving.Statistics.StaticShadowMapUpdates==1&&allMoving.Statistics.StaticShadowDrawCalls==0&&
        allMoving.Statistics.DynamicShadowDrawCalls>0,"An empty static layer retained unified depth from its previous contents");
}
}

int main(int argc,char** argv){
    IGameSproutRuntime* runtime=nullptr;IRuntimeViewport* viewport=nullptr;Provider provider;std::string error;
    try{
        Check(argc==2,"Pass Runtime binary directory");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        viewport=runtime->CreateOffscreenRenderer(256,256,&provider,error);
        if(!viewport){delete runtime;std::cout<<"SKIP GPU unavailable: "<<error<<'\n';return 77;}
        auto* scene=runtime->CreateScene("Directional shadow GPU checks");
        auto* receiver=MeshObject(*scene,"Receiving plane",1);
        auto* caster=MeshObject(*scene,"Off-camera casting cube",2);
        caster->GetTransform()->SetLocalPosition({2.7f,0,2});Check(caster->SetLayer(1),"Cannot assign casting layer");
        auto* lightParent=scene->CreateGameObject("Light parent");
        auto* sunObject=scene->CreateGameObject("Sun",lightParent);auto* sun=sunObject->AddComponent<ILightComponent>();
        // Rays follow +Z: 225 degrees gives surface-to-light (+X,0,+Z).
        Check(sun&&sunObject->GetTransform()->SetLocalRotation(Yaw(225)),"Cannot create/aim shadow light");
        auto settings=sun->GetSettings();settings.Intensity=.5f;settings.CullingMask=3;
        settings.Shadows.Enabled=false;settings.Shadows.Resolution=512;settings.Shadows.Distance=20;
        settings.Shadows.CascadeCount=1;settings.Shadows.Strength=1;
        settings.Shadows.DepthBias=.5f;settings.Shadows.NormalBias=1;settings.Shadows.Soft=false;
        Check(sun->SetSettings(settings),"Cannot configure shadow light");
        EnvironmentLightingSettings environment;environment.Mode=AmbientLightMode::Color;environment.AmbientColor={.04f,.04f,.04f,1};
        Check(scene->SetEnvironmentLighting(environment),"Cannot set shadow-test ambient");
        RenderView view;view.Scene=scene;view.DrawGrid=false;view.ClearColor={0,0,0,1};view.Camera.Position={0,0,5};
        view.Camera.Projection.Projection=CameraProjection::Orthographic;view.Camera.Projection.OrthographicHeight=4;
        view.Camera.Projection.NearPlane=.1f;view.Camera.Projection.FarPlane=30;
        Check(TryMakeLookAt(view.Camera.Position,{0,0,0},{0,1,0},view.Camera.View),"Cannot aim shadow camera");
        const auto baseline=Render(*viewport,view);
        Check(baseline.Statistics.VisibleObjects==1&&baseline.Statistics.ShadowDrawCalls==0,
            "Fixture must render only the receiving plane, with its caster outside the camera frustum");

        Frame hard;
        for(unsigned count:{1u,2u,4u}){
            settings.Shadows.Enabled=true;settings.Shadows.CascadeCount=count;settings.Shadows.Soft=false;
            Check(sun->SetSettings(settings),"Cannot enable cascade configuration");
            hard=Render(*viewport,view);EffectiveShadow(baseline,hard,"Hard cascades");
            Check(hard.Statistics.ShadowMapUpdates>0&&hard.Statistics.ShadowDrawCalls>0&&hard.Statistics.ShadowCascadeCount==count,
                "Shadow pass counters did not describe the selected cascade configuration");
            const auto cached=Render(*viewport,view);
            Check(cached.Statistics.ShadowMapUpdates==0&&cached.Statistics.ShadowDrawCalls==0&&Difference(hard,cached)==0,
                "Unchanged camera/scene redrew its shadow atlas or changed the cached image");
            auto clearOnly=view;clearOnly.DrawScene=false;
            Check(viewport->Render(clearOnly)&&viewport->GetStatistics().DrawCalls==0,"Clear-only frame rendered scene geometry");
            const auto resumed=Render(*viewport,view);
            Check(resumed.Statistics.ShadowMapUpdates==0&&Difference(hard,resumed)==0,
                "Clear-only frame disabled or destroyed a reusable shadow atlas");
            settings.Shadows.Soft=true;Check(sun->SetSettings(settings),"Cannot enable PCF");
            const auto soft=Render(*viewport,view);EffectiveShadow(baseline,soft,"Soft cascades");
        }

        settings.Shadows.CascadeCount=1;settings.Shadows.Soft=false;Check(sun->SetSettings(settings),"Cannot restore hard shadow");
        hard=Render(*viewport,view);const auto originalRegion=Darkened(baseline,hard);
        settings.Shadows.Strength=0;Check(sun->SetSettings(settings),"Cannot disable shadow strength");
        const auto zeroStrength=Render(*viewport,view);
        Check(Difference(baseline,zeroStrength)==0,"Zero strength retained darkening");
        settings.Shadows.Strength=1;settings.Shadows.Enabled=false;Check(sun->SetSettings(settings),"Cannot disable shadow pass");
        const auto disabled=Render(*viewport,view);
        Check(Difference(baseline,disabled)==0&&disabled.Statistics.ShadowDrawCalls==0,"Disabled shadows retained sampling or pass work");

        // Excluding layer 1 must remove the invisible caster without removing
        // illumination of the receiving plane in layer 0.
        settings.Shadows.Enabled=true;settings.CullingMask=1;Check(sun->SetSettings(settings),"Cannot exclude casting layer");
        const auto excluded=Render(*viewport,view);
        Check(Difference(baseline,excluded)<16,"Light layer exclusion left the excluded object's shadow");
        settings.CullingMask=3;Check(sun->SetSettings(settings),"Cannot restore casting layer");
        EffectiveShadow(baseline,Render(*viewport,view),"Restored casting layer");

        // The light's parent rotation changes both the illumination and shadow
        // direction. Compare against its own unshadowed image, not a brighter
        // image taken under the old light orientation.
        Check(lightParent->GetTransform()->SetLocalRotation(Yaw(15)),"Cannot rotate shadow light parent");
        settings.Shadows.Enabled=false;sun->SetSettings(settings);const auto rotatedBaseline=Render(*viewport,view);
        settings.Shadows.Enabled=true;sun->SetSettings(settings);const auto rotated=Render(*viewport,view);
        EffectiveShadow(rotatedBaseline,rotated,"Rotated light parent");
        const auto rotatedRegion=Darkened(rotatedBaseline,rotated);
        Check(rotated.Statistics.ShadowMapUpdates>0&&std::fabs(rotatedRegion.CenterX-originalRegion.CenterX)>25,
            "Parent rotation left the old light-space projection/sampling coordinates");

        lightParent->GetTransform()->SetLocalRotation({});const auto beforeResource=Render(*viewport,view);
        provider.CubeHalfSize=.65f;++provider.Revision;const auto newResource=Render(*viewport,view);
        EffectiveShadow(baseline,newResource,"Changed provider mesh");
        Check(newResource.Statistics.ShadowMapUpdates>0&&Difference(beforeResource,newResource)>100&&
            Darkened(baseline,newResource).Count>Darkened(baseline,beforeResource).Count,
            "Resource revision retained old casting geometry/derived bounds/atlas");
        caster->GetTransform()->SetLocalPosition({3,0,2});const auto moved=Render(*viewport,view);
        Check(moved.Statistics.ShadowMapUpdates>0&&Difference(newResource,moved)>100,
            "Caster transform mutation did not update the atlas and projected shadow");

        view.Camera.Position={.25f,0,5};Check(TryMakeLookAt(view.Camera.Position,{.25f,0,0},{0,1,0},view.Camera.View),"Cannot move shadow camera");
        view.ReuseShadowsWhileNavigating=true;
        const auto navigating=Render(*viewport,view);
        Check(navigating.Statistics.ShadowMapUpdates==0&&navigating.Statistics.ShadowDrawCalls==0,
            "Interactive camera movement rebuilt an otherwise valid shadow atlas");
        EffectiveShadow(baseline,navigating,"Shadow atlas during camera navigation");
        view.ReuseShadowsWhileNavigating=false;
        const auto cameraChanged=Render(*viewport,view);
        Check(cameraChanged.Statistics.ShadowMapUpdates>0,"Ending navigation retained the old cascade fit");
        view.ReuseShadowsWhileNavigating=true;
        view.Camera.Projection.OrthographicHeight=4.25f;
        const auto projectionChanged=Render(*viewport,view);
        Check(projectionChanged.Statistics.ShadowMapUpdates>0,"Navigation reused shadows after a projection change");
        caster->GetTransform()->SetLocalPosition({2.8f,0,2});
        const auto casterChanged=Render(*viewport,view);
        Check(casterChanged.Statistics.ShadowMapUpdates>0,"Navigation reused shadows after a caster change");
        Check(viewport->Resize(192,256,error),error);const auto resized=Render(*viewport,view);
        Check(resized.Statistics.ShadowMapUpdates>0,"Aspect mutation retained the old cascade fit");

        Check(viewport->Resize(256,256,error),error);
        Check(receiver->SetStaticFlags(StaticFlags::ShadowCasterStatic)&&caster->SetStaticFlags(StaticFlags::ShadowCasterStatic),
            "Cannot mark the cascade retention fixture static");
        view.ReuseShadowsWhileNavigating=false;
        view.Camera.Projection={};
        view.Camera.Projection.FarPlane=50;
        const auto aim=[&](float x) {
            view.Camera.Position={x,0,20};
            Check(TryMakeLookAt(view.Camera.Position,{x,0,0},{0,1,0},view.Camera.View),"Cannot aim cascade retention camera");
        };
        settings.Shadows.Enabled=false;
        settings.Shadows.CacheStaticCasters=true;
        settings.Shadows.CascadeCount=4;
        settings.Shadows.Distance=50;
        settings.Shadows.Soft=true;
        Check(sun->SetSettings(settings),"Cannot configure cascade retention baseline");
        aim(3);
        const auto unshadowedShifted=Render(*viewport,view);
        settings.Shadows.Enabled=true;
        Check(sun->SetSettings(settings),"Cannot enable static cascade retention fixture");
        aim(0);
        const auto initialCascades=Render(*viewport,view);
        Check(initialCascades.Statistics.StaticShadowCascadesUpdated==4,"Initial atlas missed a static cascade");
        aim(3);
        const auto shiftedCascades=Render(*viewport,view);
        Check(shiftedCascades.Statistics.StaticShadowCascadesUpdated==1,
            "Small lateral camera motion rebuilt all static cascades");
        Check(Darkened(unshadowedShifted,shiftedCascades).Count>30,
            "Updating the nearest cascade erased the retained distant shadow");
        const auto cachedShifted=Render(*viewport,view);
        Check(cachedShifted.Statistics.StaticShadowCascadesUpdated==0&&Difference(shiftedCascades,cachedShifted)==0,
            "Partial update was not retained in the next unchanged frame");
        for(const Vector3 direction: {Vector3{1,.2f,0},Vector3{0,-.3f,1},Vector3{-1,0,0},Vector3{0,0,-1}}) {
            Check(TryMakeLookAt(view.Camera.Position,view.Camera.Position+direction,{0,1,0},view.Camera.View),
                "Cannot rotate cascade retention camera");
            const auto turned=Render(*viewport,view);
            Check(turned.Statistics.StaticShadowCascadesUpdated<=1,
                "Turning at a fixed camera position repainted a rotation-stable cascade");
        }

        CheckLayeredShadows(*runtime,*viewport,provider);
        CheckScrollingShadows(*runtime,*viewport,provider);
        (void)receiver;delete viewport;viewport=nullptr;delete runtime;runtime=nullptr;
        std::cout<<"PASS directional shadows GPU: off-camera casters, hard/PCF 1/2/4 cascades, strength/toggle/layers, parent rotation, resource and transform revisions, camera/aspect, and static/dynamic atlas reuse\n";
        return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';delete viewport;delete runtime;return 1;}
}
