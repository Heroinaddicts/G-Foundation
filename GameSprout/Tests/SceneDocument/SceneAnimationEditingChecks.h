#pragma once

static void CheckAnimationSceneDeletion(SceneManager& manager,GameSproutEditor::GameSproutModuleApi& api,const QDir& root) {
    auto* scene=manager.GetActiveScene();
    auto* character=scene->CreateGameObject("AnimationDelete Character");
    auto* bone=scene->CreateGameObject("AnimationDelete Bone",character);
    auto* geometry=scene->CreateGameObject("AnimationDelete Geometry",character);
    auto* animator=character->AddComponent<GameSproutRuntime::IAnimatorComponent>();
    Check(animator,"Cannot add deletion test Animator");
    const GameSproutRuntime::ResourceReference avatar{"1234567890abcdef1234567890abcdef",25};
    const GameSproutRuntime::ResourceReference replacementAvatar{"1234567890abcdef1234567890abcdef",26};
    animator->SetAvatar(avatar);
    animator->SetPlayOnStart(false);
    Check(animator->SetNodeBindings({{"Bone#0",bone->GetInstanceID()}}),"Cannot bind deletion test bone");
    geometry->AddComponent<GameSproutRuntime::IMeshComponent>();
    geometry->AddComponent<GameSproutRuntime::IMeshRendererComponent>();
    auto* skin=geometry->AddComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>();
    Check(skin&&skin->SetAnimatorObjectID(character->GetInstanceID()),"Cannot bind deletion test skin");
    GameSproutEditor::GameSproutEditorFinal::Update(manager,api);
    std::string error;
    Check(manager.SaveSceneAs("Scenes/AnimationDeletion.scene",error),error);
    const auto avatarEdit=[](GameSproutRuntime::ResourceReference reference) {
        return [reference](GameSproutRuntime::IGameObject& object,std::string& failure) {
            auto* component=object.GetComponent<GameSproutRuntime::IAnimatorComponent>();
            if(!component){failure="Animator no longer exists";return false;}
            component->SetAvatar(reference);return true;
        };
    };
    Check(manager.EditObject(character->GetInstanceID(),"Change target Avatar",avatarEdit(replacementAvatar),avatarEdit(avatar),error),error);
    Check(animator->GetAvatar()==replacementAvatar&&manager.HasUnsavedChanges(),"Target Avatar edit was not an authoring transaction");
    Check(manager.Undo(error)&&animator->GetAvatar()==avatar,"Avatar edit Undo lost the previous target identity");
    Check(manager.Redo(error)&&animator->GetAvatar()==replacementAvatar,"Avatar edit Redo lost the new target identity");
    Check(manager.Undo(error)&&animator->GetAvatar()==avatar&&manager.SaveScene(error),error);
    const auto characterFile=manager.GetObjectFileID(character->GetInstanceID());
    const auto geometryFile=manager.GetObjectFileID(geometry->GetInstanceID());
    const auto boneFile=manager.GetObjectFileID(bone->GetInstanceID());
    const auto oldGeometryID=geometry->GetInstanceID();
    // Deleting only the mesh subtree retains a reference to an Animator outside that subtree.
    Check(manager.DeleteObject(oldGeometryID,error),"Cannot delete geometry with external Animator reference: "+error);
    Check(!manager.QueryObjectByFileID(geometryFile)&&manager.SaveScene(error),"Geometry deletion was not serializable: "+error);
    Check(manager.Undo(error),"Cannot undo geometry deletion: "+error);
    geometry=manager.QueryObjectByFileID(geometryFile);skin=geometry?geometry->GetComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>():nullptr;
    Check(geometry&&geometry->GetInstanceID()!=oldGeometryID&&skin&&skin->GetAnimatorObjectID()==character->GetInstanceID(),"Undo did not restore external Animator binding to the new geometry identity");
    Check(manager.Redo(error)&&!manager.QueryObjectByFileID(geometryFile),"Cannot redo animated geometry deletion: "+error);
    Check(manager.Undo(error),"Cannot restore animated geometry for bone test: "+error);
    geometry=manager.QueryObjectByFileID(geometryFile);skin=geometry->GetComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>();
    // Deleting a bone clears the binding on a surviving Animator; Undo must rebind to a new ID.
    const auto oldBoneID=bone->GetInstanceID();
    Check(manager.DeleteObject(oldBoneID,error),"Cannot delete animated bone: "+error);
    Check(animator->GetNodeBindings()[0].ObjectID==0&&manager.SaveScene(error),"Deleted bone left an unsavable source reference: "+error);
    Check(manager.Undo(error),"Cannot undo bone deletion: "+error);
    bone=manager.QueryObjectByFileID(boneFile);
    Check(bone&&bone->GetInstanceID()!=oldBoneID&&animator->GetNodeBindings()[0].ObjectID==bone->GetInstanceID(),"Undo did not restore an external Animator binding to the new bone ID");
    Check(manager.Redo(error)&&animator->GetNodeBindings()[0].ObjectID==0,"Bone deletion Redo failed to clear bindings: "+error);
    Check(manager.Undo(error),"Cannot restore bone before deleting a complete model: "+error);
    // Complete subtree recreation changes every Runtime ID while preserving all file references.
    Check(manager.DeleteObject(character->GetInstanceID(),error),"Cannot delete complete animated model: "+error);
    Check(!manager.QueryObjectByFileID(characterFile)&&manager.SaveScene(error),"Deleted model was not persisted: "+error);
    Check(manager.Undo(error),"Cannot undo complete animated model deletion: "+error);
    character=manager.QueryObjectByFileID(characterFile);bone=manager.QueryObjectByFileID(boneFile);geometry=manager.QueryObjectByFileID(geometryFile);
    animator=character?character->GetComponent<GameSproutRuntime::IAnimatorComponent>():nullptr;
    skin=geometry?geometry->GetComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>():nullptr;
    Check(character&&bone&&geometry&&animator&&skin&&animator->GetAvatar()==avatar&&animator->GetNodeBindings()[0].ObjectID==bone->GetInstanceID()&&skin->GetAnimatorObjectID()==character->GetInstanceID(),"Restored model contains mixed old/new Runtime bindings or lost Avatar");
    Check(manager.Redo(error)&&!manager.QueryObjectByFileID(characterFile),"Complete animated model Redo failed: "+error);
    Check(manager.Undo(error)&&manager.SaveScene(error),"Cannot restore and save complete animated model: "+error);
    Check(manager.OpenScene("Scenes/AnimationDeletion.scene",error),error);
    scene=manager.GetActiveScene();character=scene->FindChild("AnimationDelete Character");
    bone=character?character->FindChild("AnimationDelete Bone"):nullptr;geometry=character?character->FindChild("AnimationDelete Geometry"):nullptr;
    animator=character?character->GetComponent<GameSproutRuntime::IAnimatorComponent>():nullptr;
    skin=geometry?geometry->GetComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>():nullptr;
    Check(animator&&skin&&bone&&animator->GetAvatar()==avatar&&animator->GetNodeBindings()[0].ObjectID==bone->GetInstanceID()&&skin->GetAnimatorObjectID()==character->GetInstanceID(),"Reopened animated model lost bindings or target Avatar");
    Check(QFile::exists(root.filePath("Assets/Scenes/AnimationDeletion.scene")),"Animation deletion test scene was not written");
}
