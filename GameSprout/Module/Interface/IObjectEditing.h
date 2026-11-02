#pragma once
#include "ISceneManager.h"

struct ObjectEditTarget {
    std::uint64_t SceneID = 0;
    std::uint64_t ObjectID = 0;
    // Zero addresses object properties; otherwise requires this live component instance.
    std::uint64_t ComponentID = 0;
    bool operator==(const ObjectEditTarget&) const = default;
};

struct ObjectEditEvent {
    ObjectEditTarget Target;
    bool Playing = false;
};

class IObjectEditingListener {
public:
    virtual ~IObjectEditingListener() = default;
    virtual void OnObjectPropertiesChanged(const ObjectEditEvent&) = 0;
};

// Property commands only. Object/component structure and source saving remain
// authoring operations. Identity values can cross a future IPC boundary; setter
// closures execute synchronously in this process and never cross that boundary.
class IObjectEditing : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~IObjectEditing() = default;
    virtual bool CanEdit(const ObjectEditTarget&, std::string& error) const = 0;
    // Edit delegates to the authoring transaction. Playing/Paused writes only the
    // transient world: no Capture, FileID allocation, author history or dirty state.
    // Closures capture values and resolve components again from the supplied object.
    virtual bool EditObject(const ObjectEditTarget&, const std::string& label,
        const ISceneManager::fSceneEditOperation& apply,
        const ISceneManager::fSceneEditOperation& revert, std::string& error) = 0;
    virtual bool SetLayer(const ObjectEditTarget&, std::uint8_t layer,
        bool includeChildren, std::string& error) = 0;
    // Owner thread, synchronous completion; listener callbacks cannot reenter edits.
    virtual bool RegisterEditingListener(IObjectEditingListener*) = 0;
    virtual void UnregisterEditingListener(IObjectEditingListener*) = 0;
};
