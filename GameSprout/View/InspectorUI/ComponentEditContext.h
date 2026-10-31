#pragma once
#include "ISceneManager.h"
#include <functional>
class IAssetsManager;
class ILibraryManager;
class ISelectionManager;

struct ComponentEditContext {
    ISceneManager* Scenes = nullptr;
    IAssetsManager* Assets = nullptr;
    ILibraryManager* Library = nullptr;
    ISelectionManager* Selection = nullptr;
    std::uint64_t SceneID = 0;
    std::uint64_t ObjectID = 0;
    std::function<void(const std::string&)> Report;
    GameSproutRuntime::IGameObject* Resolve() const;
    bool Execute(const std::string& label, const ISceneManager::fSceneEditOperation& apply,
                 const ISceneManager::fSceneEditOperation& revert) const;
    std::string Key(const std::string& component, const std::string& field) const;
};
