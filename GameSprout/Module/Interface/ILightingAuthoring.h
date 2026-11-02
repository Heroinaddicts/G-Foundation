#pragma once
#include "GameSproutEditorApi.h"
#include "IObjectEditing.h"
#include "RuntimeLightingApi.h"

struct EnvironmentLightingSnapshot {
    std::uint64_t SceneID = 0;
    bool Available = false;
    bool Editable = false;
    GameSproutRuntime::EnvironmentLightingSettings Settings;
};
class ILightingEnvironmentListener {
public:
    virtual ~ILightingEnvironmentListener() = default;
    virtual void OnEnvironmentLightingChanged(const EnvironmentLightingSnapshot&) = 0;
};

// Values and identities only. Node/component pointers never outlive a call.
class ILightingAuthoring : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~ILightingAuthoring() = default;
    virtual bool CanCreate(std::uint64_t sceneID,std::uint64_t parentID,std::string& error) const = 0;
    virtual bool CreateDirectionalLight(std::uint64_t sceneID,std::uint64_t parentID,
        std::uint64_t& createdObjectID,std::string& error) = 0;
    virtual bool ReadLight(const ObjectEditTarget&,GameSproutRuntime::LightSettings&,std::string& error) const = 0;
    virtual bool SetLight(const ObjectEditTarget&,const GameSproutRuntime::LightSettings&,std::string& error) = 0;
    virtual bool ReadEnvironment(EnvironmentLightingSnapshot&,std::string& error) const = 0;
    virtual bool SetEnvironment(std::uint64_t sceneID,const GameSproutRuntime::EnvironmentLightingSettings&,std::string& error) = 0;
    // Main thread; subscribe then read once. Later changes are pushed as values.
    virtual bool RegisterEnvironmentListener(ILightingEnvironmentListener*) = 0;
    virtual void UnregisterEnvironmentListener(ILightingEnvironmentListener*) = 0;
};
