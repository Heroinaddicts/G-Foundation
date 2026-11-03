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
        const auto cameraChanged=Render(*viewport,view);
        Check(cameraChanged.Statistics.ShadowMapUpdates>0,"Camera mutation retained the old cascade atlas");
        Check(viewport->Resize(192,256,error),error);const auto resized=Render(*viewport,view);
        Check(resized.Statistics.ShadowMapUpdates>0,"Aspect mutation retained the old cascade fit");

        (void)receiver;delete viewport;viewport=nullptr;delete runtime;runtime=nullptr;
        std::cout<<"PASS directional shadows GPU: off-camera casters, hard/PCF 1/2/4 cascades, strength/toggle/layers, parent rotation, resource and transform revisions, camera/aspect and atlas reuse\n";
        return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';delete viewport;delete runtime;return 1;}
}
