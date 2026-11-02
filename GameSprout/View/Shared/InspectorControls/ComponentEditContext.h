#pragma once
#include "ISceneManager.h"
#include "IAnimationPreview.h"
#include <functional>
class IAssetsManager;
class ILibraryManager;
class ISelectionManager;
class IInspectorUI;
class IRuntimeHost;
class IScriptEditor;
class IObjectEditing;

struct ComponentEditContext {
    ISceneManager* Scenes = nullptr;
    IObjectEditing* Editing = nullptr;
    GameSproutRuntime::IGameSproutRuntime* Runtime = nullptr;
    IAnimationPreview* Preview = nullptr;
    IAssetsManager* Assets = nullptr;
    ILibraryManager* Library = nullptr;
    ISelectionManager* Selection = nullptr;
    IInspectorUI* Inspector = nullptr;
    IRuntimeHost* Host = nullptr;
    IScriptEditor* Scripts = nullptr;
    std::uint64_t SceneID = 0;
    std::uint64_t ObjectID = 0;
    // Live owner-local component identity; never keep a borrowed component pointer.
    std::uint64_t ComponentID = 0;
    std::function<void(const std::string&)> Report;
    GameSproutRuntime::IGameObject* Resolve() const;
    GameSproutRuntime::IComponent* ResolveComponent() const;
    bool Execute(const std::string& label, const ISceneManager::fSceneEditOperation& apply,
                 const ISceneManager::fSceneEditOperation& revert) const;
    std::string Key(const std::string& component, const std::string& field) const;
};
