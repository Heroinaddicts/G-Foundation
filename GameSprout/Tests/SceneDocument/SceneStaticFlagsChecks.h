#pragma once
#include "PrefabDocument.h"

static void CheckStaticFlagDocuments(GameSproutRuntime::IGameSproutRuntime& runtime) {
    using GameSproutRuntime::StaticFlags;
    std::string error;
    auto* scene=runtime.CreateScene("Static declarations");auto* root=scene->CreateGameObject("Building");auto* child=scene->CreateGameObject("Door",root);
    Check(scene->SetStaticFlags(StaticFlags::ReflectionProbeStatic)&&root->SetStaticFlags(StaticFlags::ContributeGI|StaticFlags::BatchingStatic)&&
        child->SetStaticFlags(StaticFlags::OccludeeStatic),"Cannot prepare static declarations");
    SceneDocument::ObjectIDs ids;Json::Value document;
    Check(SceneDocument::Serialize(*scene,ids,document,error),error);
    Check(document["RootStaticFlags"].asUInt()==64&&document["Objects"][0]["StaticFlags"].asUInt()==5&&document["Objects"][1]["StaticFlags"].asUInt()==16,
        "Scene source omitted static declarations");
    GameSproutRuntime::IScene* restored=nullptr;SceneDocument::ObjectIDs restoredIDs;
    Check(SceneDocument::Instantiate(document,runtime,restored,restoredIDs,error),error);
    Check(restored->GetStaticFlags()==StaticFlags::ReflectionProbeStatic&&restored->GetChild(0)->GetStaticFlags()==(StaticFlags::ContributeGI|StaticFlags::BatchingStatic)&&
        restored->GetChild(0)->GetChild(0)->GetStaticFlags()==StaticFlags::OccludeeStatic,"Static declarations lost or inherited across scene roundtrip");
    for(const auto invalid:{Json::Value(-1),Json::Value(128),Json::Value(Json::UInt64(1)<<40),Json::Value(1.5)}) {
        auto bad=document;bad["Objects"][0]["StaticFlags"]=invalid;const auto count=runtime.GetSceneCount();auto* preserved=restored;
        Check(!SceneDocument::Validate(bad,error)&&!SceneDocument::Instantiate(bad,runtime,preserved,error)&&preserved==restored&&runtime.GetSceneCount()==count,
            "Invalid static declaration accepted or created a partial scene");
    }
    auto bad=document;bad["RootStaticFlags"]=128;Check(!SceneDocument::Validate(bad,error),"Invalid root static declaration accepted");
    auto prefab=document;prefab.removeMember("RootComponents");prefab.removeMember("RootStaticFlags");prefab.removeMember("RootLayer");prefab.removeMember("RootTagID");
    prefab.removeMember("RootActive");prefab["MainCameraID"]=Json::UInt64(0);
    Check(PrefabDocument::Validate(prefab,error),error);
    GameSproutRuntime::IGameObject* instance=nullptr;std::vector<GameSproutRuntime::PrefabObjectMapping> mappings;
    Check(SceneDocument::InstantiatePrefab(prefab,runtime,*restored,*restored,instance,mappings,error),error);
    Check(instance->GetStaticFlags()==root->GetStaticFlags()&&instance->GetChild(0)->GetStaticFlags()==child->GetStaticFlags(),"Prefab assembly lost StaticFlags");
    Check(runtime.DestroyScene(scene)&&runtime.DestroyScene(restored),"Cannot release static source fixture");
}
