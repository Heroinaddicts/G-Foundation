#pragma once
#include "GameSproutEditorApi.h"
#include "RuntimeStaticFlags.h"

struct StaticFlagDefinition {
    GameSproutRuntime::StaticFlags Flag = GameSproutRuntime::StaticFlags::None;
    std::string Name;
    std::string Description;
    bool Supported = false;
};

// Authoring policy only. Runtime stores flags; actual baking systems own their capabilities.
class IStaticAuthoring : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~IStaticAuthoring() = default;
    virtual std::vector<StaticFlagDefinition> GetFlagDefinitions() const = 0;
    virtual GameSproutRuntime::StaticFlags GetSupportedFlags() const = 0;
    // Changed flags are updated independently for each object, preserving all other bits.
    // Unsupported systems cannot be enabled; clearing existing authoring data remains possible.
    virtual bool SetFlags(std::uint64_t objectID, GameSproutRuntime::StaticFlags values,
        GameSproutRuntime::StaticFlags changedFlags, bool includeChildren, std::string& error) = 0;
};
