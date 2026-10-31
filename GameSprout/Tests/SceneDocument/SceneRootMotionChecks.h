#pragma once
#include "SceneMotionFixture.h"
#include <cmath>

static void CheckRootMotionPersistence(GameSproutRuntime::IGameSproutRuntime& runtime) {
    SceneMotionResources resources;
    auto* previous=runtime.GetResourceProvider();runtime.SetResourceProvider(&resources);
    struct ProviderScope {
        GameSproutRuntime::IGameSproutRuntime& Runtime;
        GameSproutRuntime::IRuntimeResourceProvider* Previous;
        ~ProviderScope(){Runtime.SetResourceProvider(Previous);}
    } providerScope{runtime,previous};
    auto* scene=runtime.CreateScene("Root motion source");
    auto* object=scene->CreateGameObject("Character");auto* bone=scene->CreateGameObject("Bone",object);
    object->GetTransform()->SetLocalPosition({10,0,0});
    auto* animator=object->AddComponent<GameSproutRuntime::IAnimatorComponent>();
    const std::string guid="0123456789abcdef0123456789abcdef";
    animator->SetSkeleton({guid,1});animator->SetDefaultClip({guid,2});animator->SetPlayOnStart(false);
    Check(animator->SetNodeBindings({{"/Bone#0",bone->GetInstanceID()}}),"Cannot bind root motion scene");
    Check(animator->SetRootMotionMode(GameSproutRuntime::RootMotionMode::ApplyToObject),"Cannot configure applied root motion");
    SceneMotionListener listener;listener.Object=object->GetInstanceID();Check(runtime.RegisterListener(&listener),"Cannot register motion listener");
    struct ListenerScope {
        GameSproutRuntime::IGameSproutRuntime& Runtime;SceneMotionListener& Listener;
        ~ListenerScope(){Runtime.UnregisterListener(&Listener);}
    } listenerScope{runtime,listener};
    Check(animator->Play(),animator->GetLastError());runtime.Update(0.5f);
    Check(std::fabs(object->GetTransform()->GetWorldMatrix().Data[12]-11)<0.0001f,"Root motion was not applied to the runtime matrix");
    Check(object->GetTransform()->GetLocalPosition().X==10&&bone->GetTransform()->GetLocalPosition().X==0,"Runtime movement modified editing TRS");
    Check(std::fabs(bone->GetTransform()->GetWorldMatrix().Data[12]-11)<0.0001f,"Bone displacement was applied twice");
    Check(listener.Steps==1&&listener.Enters==1&&listener.Moves==1,"Animation notifications did not cross the expected interval");
    Check(object->GetTransform()->IsAnimationDriven(),"Applied root motion was not identified as an evaluated transform");
    SceneDocument::ObjectIDs ids;Json::Value document;std::string error;
    Check(SceneDocument::Serialize(*scene,ids,document,error),error);
    const auto& character=FileNode(document,"Character");
    for(const auto& component:character["Components"]) {
        if(component["Type"]=="GameSproutRuntime.Transform")Check(component["Position"][0].asFloat()==10,"Scene saved the running root trajectory");
        if(component["Type"]=="GameSproutRuntime.Animator") {
            Check(component["RootMotionMode"]=="ApplyToObject","Scene omitted root motion configuration");
            Check(!component.isMember("RootMotionDelta")&&!component.isMember("Time"),"Scene saved transient animation state");
        }
    }
    GameSproutRuntime::IScene* loaded=nullptr;SceneDocument::ObjectIDs loadedIDs;
    Check(SceneDocument::Instantiate(document,runtime,loaded,loadedIDs,error),error);
    auto* restored=loaded->FindChild("Character");
    Check(restored->GetComponent<GameSproutRuntime::IAnimatorComponent>()->GetRootMotionMode()==GameSproutRuntime::RootMotionMode::ApplyToObject,
        "Scene did not restore root motion mode");
    Check(restored->GetTransform()->GetWorldMatrix().Data[12]==10,"Scene reopened at a transient playback position");
    auto withoutMode=document;
    for(auto& node:withoutMode["Objects"])for(auto& component:node["Components"])
        if(component["Type"]=="GameSproutRuntime.Animator")component.removeMember("RootMotionMode");
    GameSproutRuntime::IScene* defaults=nullptr;Check(SceneDocument::Instantiate(withoutMode,runtime,defaults,error),error);
    Check(defaults->FindChild("Character")->GetComponent<GameSproutRuntime::IAnimatorComponent>()->GetRootMotionMode()==GameSproutRuntime::RootMotionMode::Disabled,
        "Omitted optional root motion mode did not retain the normal default");
    auto invalid=document;
    for(auto& node:invalid["Objects"])for(auto& component:node["Components"])
        if(component["Type"]=="GameSproutRuntime.Animator")component["RootMotionMode"]="Unknown";
    const auto count=runtime.GetSceneCount();auto* preserved=loaded;
    Check(!SceneDocument::Instantiate(invalid,runtime,loaded,error)&&loaded==preserved&&runtime.GetSceneCount()==count,"Invalid motion mode mutated the scene");
    animator->Stop();Check(!object->GetTransform()->IsAnimationDriven()&&object->GetTransform()->GetWorldMatrix().Data[12]==10,"Stop failed to restore the editing pose");
    listener.Consume=true;Check(animator->Play(),animator->GetLastError());runtime.Update(0.5f);
    Check(object->GetTransform()->GetWorldMatrix().Data[12]==10,"Consumed root motion was also applied automatically");
    runtime.UnregisterListener(&listener);
    runtime.DestroyScene(defaults);runtime.DestroyScene(loaded);runtime.DestroyScene(scene);
}
