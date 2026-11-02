#pragma once
#include "PrefabDocument.h"

static void CheckTagLayerDocuments(GameSproutRuntime::IGameSproutRuntime& runtime) {
    std::string error;
    auto settings=runtime.GetProjectSettings();
    const auto enemy=settings.NextTagID++;
    settings.Tags.push_back({enemy,"Enemy"});settings.LayerNames[3]="Characters";
    Check(runtime.SetProjectSettings(settings,error),error);
    auto* scene=runtime.CreateScene("TagLayer document");
    auto* parent=scene->CreateGameObject("Parent");auto* child=scene->CreateGameObject("Child",parent);
    Check(parent->SetLayer(3)&&parent->SetTagID(enemy)&&child->SetLayer(31),"Cannot set authored Layer/Tag");
    Check(scene->SetLayer(2)&&scene->SetTagID(enemy),"Cannot set scene root Layer/Tag");
    SceneDocument::ObjectIDs ids;Json::Value document;
    Check(SceneDocument::Serialize(*scene,ids,document,error),error);
    Check(document["RootLayer"].asUInt()==2&&document["RootTagID"].asUInt64()==enemy&&
        document["Objects"][0]["Layer"].asUInt()==3&&document["Objects"][0]["TagID"].asUInt64()==enemy&&
        document["Objects"][1]["Layer"].asUInt()==31,"Source document omitted Tag/Layer identities");
    GameSproutRuntime::IScene* restored=nullptr;SceneDocument::ObjectIDs restoredIDs;
    Check(SceneDocument::Instantiate(document,runtime,restored,restoredIDs,error),error);
    Check(restored->GetTagID()==enemy&&restored->GetLayer()==2&&restored->GetChild(0)->GetLayer()==3&&
        restored->GetChild(0)->CompareTag("Enemy")&&restored->GetChild(0)->GetChild(0)->GetLayer()==31,
        "Scene roundtrip lost Tag/Layer or implicitly inherited a parent Layer");
    settings.Tags.back().Name="Opponent";Check(runtime.SetProjectSettings(settings,error),error);
    Check(restored->GetChild(0)->GetTagID()==enemy&&restored->GetChild(0)->GetTag()=="Opponent",
        "Renaming project Tag changed serialized identity");
    auto bad=document;bad["Objects"][0]["Layer"]=32;
    Check(!SceneDocument::Validate(bad,error),"Out-of-range Layer accepted");
    bad=document;bad["Objects"][0]["TagID"]=-1;
    Check(!SceneDocument::Validate(bad,error),"Negative TagID accepted");
    bad=document;bad["Objects"][0]["TagID"]=Json::UInt64(settings.NextTagID+100);
    const auto count=runtime.GetSceneCount();auto* output=restored;
    Check(!SceneDocument::Instantiate(bad,runtime,output,error)&&output==restored&&runtime.GetSceneCount()==count,
        "Undefined Tag instantiated a partial scene");
    auto prefab=document;prefab.removeMember("RootComponents");prefab.removeMember("RootLayer");prefab.removeMember("RootTagID");
    prefab.removeMember("RootActive");prefab.removeMember("EnvironmentLighting");prefab["MainCameraID"]=Json::UInt64(0);
    Check(PrefabDocument::Validate(prefab,error),error);
    GameSproutRuntime::IGameObject* instance=nullptr;std::vector<GameSproutRuntime::PrefabObjectMapping> mappings;
    Check(SceneDocument::InstantiatePrefab(prefab,runtime,*restored,*restored,instance,mappings,error),error);
    Check(instance&&instance->GetLayer()==3&&instance->GetTagID()==enemy&&instance->GetChild(0)->GetLayer()==31,
        "Prefab assembly lost Tag/Layer identities");
    Check(runtime.DestroyScene(scene)&&runtime.DestroyScene(restored),"Cannot clean classification fixture");
}
