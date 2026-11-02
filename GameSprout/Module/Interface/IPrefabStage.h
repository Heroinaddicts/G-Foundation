#pragma once
#include "GameSproutEditorApi.h"
#include <string>

struct PrefabStageState {
    bool Active=false,Dirty=false,MissingSource=false,ExternalConflict=false;
    std::string Guid,Path,Error;
    uint64_t SceneID=0,ContentRootObjectID=0;
};
class IPrefabStageListener {
public:
    virtual ~IPrefabStageListener()=default;
    virtual void OnPrefabStageChanged(const PrefabStageState&)=0;
};
// Editor authoring only. Runtime owns the separate scene; this module owns the
// source transaction and stable identities, never controls or renderer objects.
class IPrefabStage : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~IPrefabStage()=default;
    virtual PrefabStageState GetState() const=0;
    // Main-thread host safe point. Opening another source requires resolving
    // the current dirty document first. Source GUID identifies the whole prefab.
    virtual bool OpenPrefab(const std::string& guid,std::string& error)=0;
    virtual bool Save(std::string& error)=0;
    // discardChanges must be explicit when Dirty; false refuses losing edits.
    virtual bool Close(bool discardChanges,std::string& error)=0;
    virtual bool RegisterStageListener(IPrefabStageListener*)=0;
    virtual void UnregisterStageListener(IPrefabStageListener*)=0;
};
