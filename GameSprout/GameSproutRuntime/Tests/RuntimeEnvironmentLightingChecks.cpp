#include "GameSproutRuntimeExport.h"
#include "SceneDocument.h"
#include "PrefabDocument.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
class Listener final:public GameSproutRuntime::IRuntimeListener {
public:
    unsigned Changes=0;std::uint64_t SceneID=0,ObjectID=0;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event) override {
        if(event.Type==GameSproutRuntime::SceneNodeEventType::EnvironmentLightingChanged) {
            ++Changes;SceneID=event.SceneID;ObjectID=event.ObjectID;
        }
    }
};
}
int main(int argc,char** argv) {
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        Check(argc==2,"Runtime directory required");runtime=Export(argv[1]);Check(runtime,"Runtime load failed");
        auto* scene=runtime->CreateScene("Environment");auto* other=runtime->CreateScene("Isolated");
        Check(scene&&other&&scene->GetEnvironmentLighting().Mode==GameSproutRuntime::AmbientLightMode::None,"Environment must default to None");
        runtime->ProcessPendingEvents();Listener listener;Check(runtime->RegisterListener(&listener),"Environment event registration failed");
        auto settings=scene->GetEnvironmentLighting();settings.Mode=GameSproutRuntime::AmbientLightMode::SphericalHarmonics;
        settings.AmbientColor={4,.5f,.25f,1};settings.Intensity=2.5f;
        settings.Atmosphere.Enabled=true;settings.Atmosphere.DrawSky=false;
        settings.Atmosphere.ZenithColor={.1f,.3f,.7f,1};settings.Atmosphere.FogColor={.4f,.6f,.8f,1};
        settings.Atmosphere.FogStart=80;settings.Atmosphere.FogEnd=700;settings.Atmosphere.FogMaxOpacity=.7f;
        settings.SHCoefficients[0]={.2f,.3f,.4f};settings.SHCoefficients[6]={-.5f,.125f,-.25f};
        Check(scene->SetEnvironmentLighting(settings),"Signed SH and linear HDR must be valid");runtime->ProcessPendingEvents();
        Check(listener.Changes==1&&listener.SceneID==scene->GetInstanceID()&&listener.ObjectID==scene->GetInstanceID(),"Wrong environment root event identity");
        Check(other->GetEnvironmentLighting().Mode==GameSproutRuntime::AmbientLightMode::None,"Environment leaked to another scene");
        Check(scene->SetEnvironmentLighting(settings),"Unchanged valid environment rejected");runtime->ProcessPendingEvents();
        Check(listener.Changes==1,"Unchanged environment emitted duplicate event");
        settings.Atmosphere.FogStart=90;
        Check(scene->SetEnvironmentLighting(settings),"Atmosphere-only edit rejected");runtime->ProcessPendingEvents();
        Check(listener.Changes==2&&scene->GetEnvironmentLighting().Atmosphere.FogStart==90,"Atmosphere-only edit did not publish an environment event");
        const auto invalid=[&](GameSproutRuntime::EnvironmentLightingSettings value) {
            Check(!scene->SetEnvironmentLighting(value)&&scene->GetEnvironmentLighting().Mode==settings.Mode&&
                scene->GetEnvironmentLighting().Intensity==settings.Intensity&&scene->GetEnvironmentLighting().AmbientColor.R==4&&
                scene->GetEnvironmentLighting().SHCoefficients[6].X==-.5f,"Invalid environment was not atomically rejected");
        };
        auto badSettings=settings;badSettings.Mode=static_cast<GameSproutRuntime::AmbientLightMode>(255);invalid(badSettings);
        badSettings=settings;badSettings.Intensity=-1;invalid(badSettings);
        badSettings=settings;badSettings.AmbientColor.G=-.1f;invalid(badSettings);
        badSettings=settings;badSettings.SkyColor.B=std::numeric_limits<float>::infinity();invalid(badSettings);
        badSettings=settings;badSettings.SHCoefficients[8].X=std::numeric_limits<float>::quiet_NaN();invalid(badSettings);
        badSettings=settings;badSettings.Atmosphere.FogStart=-1;invalid(badSettings);
        badSettings=settings;badSettings.Atmosphere.FogEnd=settings.Atmosphere.FogStart;invalid(badSettings);
        badSettings=settings;badSettings.Atmosphere.FogMaxOpacity=1.1f;invalid(badSettings);
        badSettings=settings;badSettings.Atmosphere.ZenithColor.R=std::numeric_limits<float>::infinity();invalid(badSettings);
        runtime->ProcessPendingEvents();Check(listener.Changes==2,"Rejected environment emitted a notification");runtime->UnregisterListener(&listener);

        auto* object=scene->CreateGameObject("Prefab object");Check(object,"Cannot create ordinary object");
        SceneDocument::ObjectIDs ids;Json::Value document;std::string error;
        Check(SceneDocument::Serialize(*scene,ids,document,error),error);
        Check(document["EnvironmentLighting"]["Mode"]=="SphericalHarmonics"&&
            document["EnvironmentLighting"]["SHCoefficients"].size()==9&&
            document["EnvironmentLighting"]["SHCoefficients"][6][0].asFloat()==-.5f,"Scene lost the signed SH source data");
        Check(document["EnvironmentLighting"]["Atmosphere"]["Enabled"].asBool()&&
            !document["EnvironmentLighting"]["Atmosphere"]["DrawSky"].asBool()&&
            document["EnvironmentLighting"]["Atmosphere"]["FogStart"].asFloat()==90,"Scene lost its atmosphere settings");
        GameSproutRuntime::IScene* loaded=nullptr;SceneDocument::ObjectIDs loadedIDs;
        Check(SceneDocument::Instantiate(document,*runtime,loaded,loadedIDs,error),error);
        Json::Value again;Check(SceneDocument::Serialize(*loaded,loadedIDs,again,error)&&again==document,"Environment scene round trip changed data");
        const auto reject=[&](const Json::Value& bad) {
            auto* preserved=loaded;const auto count=runtime->GetSceneCount();
            Check(!SceneDocument::Validate(bad,error)&&!SceneDocument::Instantiate(bad,*runtime,preserved,error)&&
                preserved==loaded&&runtime->GetSceneCount()==count,"Invalid environment partially instantiated a scene");
        };
        auto bad=document;bad["EnvironmentLighting"]["Exposure"]=1;reject(bad);
        bad=document;bad["EnvironmentLighting"]["Mode"]="Skybox";reject(bad);
        bad=document;bad["EnvironmentLighting"]["SHCoefficients"]=Json::Value(Json::arrayValue);reject(bad);
        bad=document;bad["EnvironmentLighting"]["SHCoefficients"][4][0]=std::numeric_limits<double>::infinity();reject(bad);
        bad=document;bad["EnvironmentLighting"]["GroundColor"][0]=-1;reject(bad);
        bad=document;bad["EnvironmentLighting"]["Atmosphere"]["Unknown"]=true;reject(bad);
        bad=document;bad["EnvironmentLighting"]["Atmosphere"]["Enabled"]=1;reject(bad);
        bad=document;bad["EnvironmentLighting"]["Atmosphere"]["FogStart"]=1000;reject(bad);
        bad=document;bad["EnvironmentLighting"]["Atmosphere"]["FogMaxOpacity"]=1.1;reject(bad);
        bad=document;bad["EnvironmentLighting"]["Atmosphere"]["FogColor"][0]=-1;reject(bad);
        bad=document;bad["EnvironmentLighting"]=Json::Value();reject(bad);
        bad=document;bad.removeMember("EnvironmentLighting");GameSproutRuntime::IScene* omitted=nullptr;
        Check(SceneDocument::Instantiate(bad,*runtime,omitted,error)&&omitted->GetEnvironmentLighting().Mode==GameSproutRuntime::AmbientLightMode::None,
            "Omitted environment must use the normal None default");

        auto prefab=document;prefab.removeMember("RootComponents");
        Check(!PrefabDocument::Validate(prefab,error),"Prefab accepted a scene environment");
        prefab.removeMember("EnvironmentLighting");std::vector<std::uint8_t> bytes;
        Check(PrefabDocument::Encode(prefab,bytes,error),error);
        GameSproutRuntime::PrefabData data;data.Bytes=bytes;std::vector<GameSproutRuntime::PrefabObjectMapping> mappings;
        Check(runtime->InstantiatePrefabData(scene,data,nullptr,mappings,error),error);
        Check(scene->GetEnvironmentLighting().Mode==settings.Mode&&scene->GetEnvironmentLighting().Intensity==2.5f,
            "Prefab instantiation overwrote its destination environment");
        Check(runtime->DestroyScene(omitted)&&runtime->DestroyScene(loaded)&&runtime->DestroyScene(other)&&runtime->DestroyScene(scene),"Cannot release environment fixtures");
        delete runtime;std::cout<<"PASS environment scene isolation, atomic validation/events, Scene persistence and Prefab exclusion\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';delete runtime;return 1;}
}
