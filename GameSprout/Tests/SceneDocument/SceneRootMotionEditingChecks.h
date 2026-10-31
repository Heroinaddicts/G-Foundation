#pragma once
#include "SceneMotionFixture.h"

static void CheckRootMotionEditing(SceneManager& manager,const QDir&) {
    auto* runtime=manager.GetRuntime();auto* previous=runtime->GetResourceProvider();
    SceneMotionResources resources;runtime->SetResourceProvider(&resources);
    struct ResourceScope {
        GameSproutRuntime::IGameSproutRuntime* Runtime;
        GameSproutRuntime::IRuntimeResourceProvider* Previous;
        ~ResourceScope(){Runtime->SetResourceProvider(Previous);}
    } resourceScope{runtime,previous};
    auto* scene=manager.GetActiveScene();auto* character=scene->CreateGameObject("Motion edit character");
    auto* bone=scene->CreateGameObject("Motion edit bone",character);
    auto* animator=character->AddComponent<GameSproutRuntime::IAnimatorComponent>();
    const std::string guid="0123456789abcdef0123456789abcdef";
    animator->SetSkeleton({guid,1});animator->SetDefaultClip({guid,2});animator->SetPlayOnStart(false);
    Check(animator->SetNodeBindings({{"/Bone#0",bone->GetInstanceID()}}),"Cannot bind the editing fixture");
    const auto mode=[](GameSproutRuntime::RootMotionMode value){return [value](GameSproutRuntime::IGameObject& object,std::string& error){
        auto* component=object.GetComponent<GameSproutRuntime::IAnimatorComponent>();
        if(component&&component->SetRootMotionMode(value))return true;
        error="Cannot configure root motion";return false;
    };};
    std::string error;
    Check(manager.EditObject(character->GetInstanceID(),"Root motion mode",mode(GameSproutRuntime::RootMotionMode::ApplyToObject),
        mode(GameSproutRuntime::RootMotionMode::Disabled),error),error);
    Check(manager.Undo(error)&&animator->GetRootMotionMode()==GameSproutRuntime::RootMotionMode::Disabled,error);
    Check(manager.Redo(error)&&animator->GetRootMotionMode()==GameSproutRuntime::RootMotionMode::ApplyToObject,error);
    Check(manager.SaveSceneAs("Scenes/RootMotionEditing.scene",error),error);
    Check(animator->Play(),animator->GetLastError());runtime->Update(0.5f);
    Check(!manager.HasUnsavedChanges(),"Evaluated root motion marked the editing scene dirty");
    Check(!manager.BeginTransformEdit(character->GetInstanceID(),"Driven root",error)&&!manager.HasTransformEdit(),
        "Gizmo began a transaction on a driven character root");
    Check(!manager.BeginTransformEdit(bone->GetInstanceID(),"Driven bone",error)&&!manager.HasTransformEdit(),
        "Gizmo began a transaction under an animated parent");
    animator->Stop();
    Check(manager.BeginTransformEdit(character->GetInstanceID(),"Restored character",error),error);
    SceneTransformState preview;preview.Position={2,0,0};
    Check(manager.PreviewTransformEdit(preview,error),error);
    Check(manager.CancelTransformEdit(error)&&character->GetTransform()->GetLocalPosition().X==0,error);
    Check(!manager.HasUnsavedChanges(),"Cancelled source edit after root motion did not restore the baseline");
}
