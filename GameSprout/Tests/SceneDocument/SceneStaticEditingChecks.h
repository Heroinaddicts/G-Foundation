#pragma once
static void CheckStaticFlagEditing(SceneManager& manager,GameSproutRuntime::IGameSproutRuntime* runtime) {
    using GameSproutRuntime::StaticFlags;
    std::string error;auto* scene=manager.GetActiveScene();
    auto* parent=scene->CreateGameObject("Static edit parent");auto* child=scene->CreateGameObject("Static edit child",parent);
    const auto parentID=parent->GetInstanceID(),childID=child->GetInstanceID();
    Check(parent->SetStaticFlags(StaticFlags::ContributeGI|StaticFlags::BatchingStatic)&&child->SetStaticFlags(StaticFlags::ReflectionProbeStatic),"Cannot prepare static history");
    Check(manager.SetObjectStaticFlags(parentID,StaticFlags::OccluderStatic,StaticFlags::OccluderStatic,true,error),error);
    Check(parent->GetStaticFlags()==(StaticFlags::ContributeGI|StaticFlags::BatchingStatic|StaticFlags::OccluderStatic)&&
        child->GetStaticFlags()==(StaticFlags::ReflectionProbeStatic|StaticFlags::OccluderStatic),"Subtree edit overwrote unrelated per-object static bits");
    Check(manager.Undo(error)&&parent->GetStaticFlags()==(StaticFlags::ContributeGI|StaticFlags::BatchingStatic)&&child->GetStaticFlags()==StaticFlags::ReflectionProbeStatic,
        "Subtree static edit was not one Undo transaction");
    Check(manager.Redo(error)&&child->GetStaticFlags()==(StaticFlags::ReflectionProbeStatic|StaticFlags::OccluderStatic),error);
    const auto before=parent->GetStaticFlags();
    Check(!manager.SetObjectStaticFlags(parentID,static_cast<StaticFlags>(128),StaticFlags::Everything,true,error)&&parent->GetStaticFlags()==before,
        "Invalid static edit mutated objects");
    Check(manager.SetObjectStaticFlags(childID,StaticFlags::None,StaticFlags::OccluderStatic,false,error)&&child->GetStaticFlags()==StaticFlags::ReflectionProbeStatic,
        "Clearing one flag changed unrelated bits");
    Check(manager.Undo(error),error);
    const auto fileID=manager.GetObjectFileID(parentID);
    Check(manager.DeleteObject(parentID,error)&&!runtime->QueryGameObject(parentID),error);
    Check(manager.Undo(error),error);
    auto* restored=manager.QueryObjectByFileID(fileID);
    Check(restored&&restored->GetStaticFlags()==before&&restored->GetChild(0)->GetStaticFlags()==(StaticFlags::ReflectionProbeStatic|StaticFlags::OccluderStatic),
        "Deletion Undo lost static declarations in RestoreSubtree");
}
