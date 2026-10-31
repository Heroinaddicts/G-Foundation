#pragma once

// Source scene saves authoring configuration and file IDs, never live playback state.
static void CheckAnimationSerialization(GameSproutRuntime::IGameSproutRuntime& runtime) {
    auto* scene=runtime.CreateScene("Animated scene");
    auto* character=scene->CreateGameObject("Character");
    auto* bone=scene->CreateGameObject("Bone",character);
    auto* geometry=scene->CreateGameObject("Geometry",character);
    bone->GetTransform()->SetLocalPosition({1,2,3});
    auto* animator=character->AddComponent<GameSproutRuntime::IAnimatorComponent>();
    Check(animator,"Cannot create Animator for scene persistence");
    const GameSproutRuntime::ResourceReference skeleton{"0123456789abcdef0123456789abcdef",12};
    const GameSproutRuntime::ResourceReference controller{"0123456789abcdef0123456789abcdef",13};
    const GameSproutRuntime::ResourceReference clip{"0123456789abcdef0123456789abcdef",14};
    const GameSproutRuntime::ResourceReference avatar{"0123456789abcdef0123456789abcdef",15};
    animator->SetSkeleton(skeleton);animator->SetController(controller);animator->SetDefaultClip(clip);
    animator->SetAvatar(avatar);
    animator->SetSpeed(1.25f);animator->SetPlayOnStart(false);animator->SetLoop(false);
    Check(animator->SetNodeBindings({{"/source/Bone#0",bone->GetInstanceID()},{"/source/Missing#0",0}}),"Cannot configure explicit or unbound animation paths");
    geometry->AddComponent<GameSproutRuntime::IMeshComponent>();
    geometry->AddComponent<GameSproutRuntime::IMeshRendererComponent>();
    auto* skin=geometry->AddComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>();
    Check(skin&&skin->SetAnimatorObjectID(character->GetInstanceID()),"Cannot configure skinned renderer");
    // Node names may change after import; bindings must retain stable object identities.
    bone->SetName("Renamed bone");
    SceneDocument::ObjectIDs ids;Json::Value saved;std::string error;
    Check(SceneDocument::Serialize(*scene,ids,saved,error),error);
    const auto& characterFile=FileNode(saved,"Character");
    const auto& boneFile=FileNode(saved,"Renamed bone");
    const auto& geometryFile=FileNode(saved,"Geometry");
    const Json::Value* animatorFile=nullptr;const Json::Value* skinFile=nullptr;
    for(const auto& component:characterFile["Components"])if(component["Type"]=="GameSproutRuntime.Animator")animatorFile=&component;
    for(const auto& component:geometryFile["Components"])if(component["Type"]=="GameSproutRuntime.SkinnedMeshRenderer")skinFile=&component;
    Check(animatorFile&&skinFile,"Animation component source data was omitted");
    Check((*animatorFile)["Bindings"][0]["ObjectFileID"]==boneFile["ID"],"Animation binding saved a Runtime ID instead of File ID");
    Check((*skinFile)["AnimatorObjectFileID"]==characterFile["ID"],"Skin binding saved a Runtime ID instead of File ID");
    Check(!animatorFile->isMember("Time")&&!animatorFile->isMember("CurrentState")&&!animatorFile->isMember("Parameters"),"Live animation state leaked into scene source");
    Check((*animatorFile)["Avatar"]["Guid"]==avatar.Guid&&(*animatorFile)["Avatar"]["LocalID"].asUInt64()==avatar.LocalID,
        "Scene lost the authored Humanoid Avatar identity");
    Check(!animatorFile->isMember("IKGoals")&&!animatorFile->isMember("LookAt")&&!animatorFile->isMember("LayerWeights"),
        "Transient IK or animation preview state leaked into scene source");
    GameSproutRuntime::IScene* loaded=nullptr;SceneDocument::ObjectIDs loadedIDs;
    Check(SceneDocument::Instantiate(saved,runtime,loaded,loadedIDs,error),error);
    auto* loadedCharacter=loaded->FindChild("Character");
    auto* loadedBone=loadedCharacter->FindChild("Renamed bone");
    auto* loadedGeometry=loadedCharacter->FindChild("Geometry");
    auto* loadedAnimator=loadedCharacter->GetComponent<GameSproutRuntime::IAnimatorComponent>();
    auto* loadedSkin=loadedGeometry->GetComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>();
    Check(loadedAnimator&&loadedSkin,"Scene did not reconstruct animation components");
    Check(loadedAnimator->GetSkeleton()==skeleton&&loadedAnimator->GetController()==controller&&loadedAnimator->GetDefaultClip()==clip,"Animation resource identities were not restored");
    Check(loadedAnimator->GetAvatar()==avatar,"Humanoid Avatar identity was not restored");
    Check(loadedAnimator->GetSpeed()==1.25f&&!loadedAnimator->GetPlayOnStart()&&!loadedAnimator->GetLoop(),"Playback authoring settings were not restored");
    Check(loadedAnimator->GetNodeBindings()[0].ObjectID==loadedBone->GetInstanceID()&&loadedSkin->GetAnimatorObjectID()==loadedCharacter->GetInstanceID(),"Animation file bindings were not remapped to the new Runtime identities");
    std::uint64_t resolved=0;Check(loadedAnimator->ResolveBone("/source/Bone#0",resolved)&&resolved==loadedBone->GetInstanceID(),"Renamed imported bone lost its animation binding");
    Json::Value savedAgain;Check(SceneDocument::Serialize(*loaded,loadedIDs,savedAgain,error)&&savedAgain==saved,"Animation scene canonical round trip changed source");
    auto malformed=saved;
    for(auto& component:malformed["Objects"][0]["Components"])if(component["Type"]=="GameSproutRuntime.Animator")component["Bindings"][0]["ObjectFileID"]=Json::UInt64(99999);
    const auto sceneCount=runtime.GetSceneCount();auto* preserved=loaded;
    Check(!SceneDocument::Instantiate(malformed,runtime,loaded,error)&&loaded==preserved&&runtime.GetSceneCount()==sceneCount,"Missing animation node reference was accepted or leaked a scene");
    Check(loaded->DestroyGameObject(loadedBone),"Cannot delete a bound bone");
    Check(loadedAnimator->GetNodeBindings()[0].ObjectID==0,"Deleting a bone left a dangling animation identity");
    Check(SceneDocument::Serialize(*loaded,loadedIDs,savedAgain,error),error);
    Check(loadedCharacter->RemoveComponent<GameSproutRuntime::IAnimatorComponent>(),"Cannot remove Animator component");
    Check(loadedSkin->GetAnimatorObjectID()==0,"Removing Animator left a dangling skin binding");
    Check(SceneDocument::Serialize(*loaded,loadedIDs,savedAgain,error),error);
    Check(!loaded->AddComponent<GameSproutRuntime::IAnimatorComponent>()&&!loaded->AddComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>(),"Special scene root accepted character animation components");
    runtime.DestroyScene(loaded);runtime.DestroyScene(scene);
}
