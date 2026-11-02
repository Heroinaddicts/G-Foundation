#pragma once
#include "GameSproutEditorApi.h"
#include "RuntimeBuiltinResources.h"

// Authoring values only. Parent 0 selects the active document's content root.
class IPrimitiveAuthoring : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~IPrimitiveAuthoring() = default;
    virtual bool CanCreate(std::uint64_t sceneID,std::uint64_t parentID,std::string& error) const = 0;
    virtual bool CreatePrimitive(std::uint64_t sceneID,std::uint64_t parentID,
        GameSproutRuntime::PrimitiveType type,std::uint64_t& createdObjectID,std::string& error) = 0;
};
