#include "GameSproutRuntimeExport.h"
#include "SceneDocument.h"
#include "PrefabDocument.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool value,const std::string& message) {if(!value)throw std::runtime_error(message);}
class Listener final : public GameSproutRuntime::IRuntimeListener {
public:
    unsigned Changes=0;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event) override {
        if(event.Type==GameSproutRuntime::SceneNodeEventType::ComponentChanged&&event.ComponentType=="GameSproutRuntime.Light")++Changes;
    }
};
}

int main(int argc,char** argv) {
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        Check(argc==2,"Runtime directory required");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        auto* scene=runtime->CreateScene("Lighting");Check(scene,"Cannot create lighting scene");
        auto* object=scene->CreateGameObject("Sun");auto* light=object->AddComponent<GameSproutRuntime::ILightComponent>();
        Check(light&&light->GetSettings().Type==GameSproutRuntime::LightType::Directional&&light->IsEnabled()&&
            light->GetIntensity()==1&&light->GetCullingMask()==0xffffffffu,"Wrong default light contract");
        Check(object->AddComponent<GameSproutRuntime::ILightComponent>()==light,"Duplicate light was allocated");
        Check(!scene->AddComponent<GameSproutRuntime::ILightComponent>(),"Scene root accepted light component");
        const auto builtins=runtime->GetBuiltinComponentTypes();
        Check(std::any_of(builtins.begin(),builtins.end(),[](const auto& item){return item.TypeID=="GameSproutRuntime.Light";}),"Light missing in builtin catalog");
        runtime->ProcessPendingEvents();Listener listener;Check(runtime->RegisterListener(&listener),"Cannot register lighting listener");
        auto settings=light->GetSettings();settings.Color={2,0.5f,0.125f,1};settings.Intensity=3.5f;settings.CullingMask=0x80000001u;
        Check(light->SetSettings(settings),"Cannot set linear HDR light");runtime->ProcessPendingEvents();
        Check(listener.Changes==1,"Light change did not emit one component notification");
        Check(light->SetSettings(settings),"Unchanged settings rejected");runtime->ProcessPendingEvents();
        Check(listener.Changes==1,"Unchanged settings emitted an event");
        auto invalid=settings;invalid.Intensity=-1;invalid.Enabled=false;
        Check(!light->SetSettings(invalid)&&light->IsEnabled()&&light->GetIntensity()==3.5f,"Invalid settings partially changed light");
        invalid=settings;invalid.Color.G=std::numeric_limits<float>::quiet_NaN();
        Check(!light->SetSettings(invalid),"NaN color accepted");
        invalid=settings;invalid.Type=static_cast<GameSproutRuntime::LightType>(99);
        Check(!light->SetSettings(invalid),"Unsupported light type accepted");
        invalid=settings;invalid.Color.A=2;Check(!light->SetSettings(invalid),"Invalid alpha accepted");
        invalid=settings;invalid.Intensity=std::numeric_limits<float>::infinity();
        Check(!light->SetSettings(invalid),"Infinite intensity accepted");
        runtime->ProcessPendingEvents();Check(listener.Changes==1,"Rejected setting emitted events");
        runtime->UnregisterListener(&listener);

        SceneDocument::ObjectIDs identities;Json::Value document;std::string error;
        Check(SceneDocument::Serialize(*scene,identities,document,error),error);
        Json::Value& serializedLight=document["Objects"][0]["Components"][1];
        Check(serializedLight["Type"]=="GameSproutRuntime.Light"&&serializedLight["Settings"]["Type"]=="Directional"&&
            serializedLight["Settings"]["Color"][0].asFloat()==2&&serializedLight["Settings"]["CullingMask"].asUInt()==0x80000001u,
            "Document lost light color/mask");
        GameSproutRuntime::IScene* reopened=nullptr;SceneDocument::ObjectIDs reloadedIDs;
        Check(SceneDocument::Instantiate(document,*runtime,reopened,reloadedIDs,error),error);
        auto* reloaded=reopened->FindChild("Sun")->GetComponent<GameSproutRuntime::ILightComponent>();
        Check(reloaded&&reloaded->GetIntensity()==3.5f&&reloaded->GetColor().R==2&&reloaded->GetCullingMask()==0x80000001u,
            "Scene reload lost light values");
        Json::Value again;Check(SceneDocument::Serialize(*reopened,reloadedIDs,again,error)&&again==document,"Light scene was not stable after reload");
        auto bad=document;bad["Objects"][0]["Components"][1]["Settings"]["Range"]=10;
        Check(!SceneDocument::Validate(bad,error)&&error.find("Range")!=std::string::npos,"Unknown light field was accepted");
        bad=document;bad["Objects"][0]["Components"][1]["Settings"]["Type"]="Point";
        Check(!SceneDocument::Validate(bad,error),"Unsupported document light type accepted");
        bad=document;bad["Objects"][0]["Components"][1]["Settings"]["CullingMask"]=Json::UInt64(0x100000000ull);
        Check(!SceneDocument::Validate(bad,error),"Oversized light mask accepted");
        bad=document;bad["Objects"][0]["Components"][1]["Settings"]["Intensity"]=-1;
        Check(!SceneDocument::Validate(bad,error),"Negative document intensity accepted");

        // Prefab has the same object-document component contract, with fresh
        // live identities for each instance and stable authored component IDs.
        auto prefabDocument=document;prefabDocument.removeMember("RootComponents");
        prefabDocument.removeMember("EnvironmentLighting");
        std::vector<std::uint8_t> bytes;Check(PrefabDocument::Encode(prefabDocument,bytes,error),error);
        Json::Value decoded;Check(PrefabDocument::Decode(bytes,decoded,error)&&decoded==prefabDocument,error);
        GameSproutRuntime::PrefabData prefab;prefab.Bytes=bytes;
        std::vector<GameSproutRuntime::PrefabObjectMapping> mappings;
        auto* instance=runtime->InstantiatePrefabData(scene,prefab,nullptr,mappings,error);Check(instance,error);
        auto* instanceLight=instance->GetComponent<GameSproutRuntime::ILightComponent>();
        Check(instanceLight&&instanceLight!=light&&instanceLight->GetIntensity()==3.5f&&instanceLight->GetColor().R==2,
            "Prefab did not instantiate independent light settings");
        auto patch=SceneDocument::SerializeComponent(*instanceLight,{});patch["Settings"]["Enabled"]=false;
        Check(SceneDocument::RestoreComponent(patch,*instance,*runtime,error)&&!instanceLight->IsEnabled(),error);
        Check(light->IsEnabled(),"Instance override changed source light");
        auto before=SceneDocument::SerializeComponent(*instanceLight,{});patch["Settings"]["Intensity"]=-2;
        Check(!SceneDocument::RestoreComponent(patch,*instance,*runtime,error)&&SceneDocument::SerializeComponent(*instanceLight,{})==before,
            "Rejected light override was not atomic");
        Check(runtime->DestroyScene(reopened)&&runtime->DestroyScene(scene),"Cannot release lighting scenes");
        delete runtime;std::cout<<"PASS Light API validation/events and Scene/Prefab persistence\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<'\n';delete runtime;return 1;}
}
