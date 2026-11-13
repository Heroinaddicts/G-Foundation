#pragma once

#include "GameSproutRuntimeApi.h"

namespace GameSproutRuntime {
enum class RuntimePropertyChange : std::uint8_t {
    None = 0,
    Authored = 1,
    TransformPose = 2,
    ScriptFields = 4
};

inline constexpr RuntimePropertyChange operator|(RuntimePropertyChange left, RuntimePropertyChange right) {
    return static_cast<RuntimePropertyChange>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
}

inline constexpr bool HasRuntimePropertyChange(RuntimePropertyChange changes, RuntimePropertyChange flag) {
    return (static_cast<std::uint8_t>(changes) & static_cast<std::uint8_t>(flag)) != 0;
}

// Uses the same host-thread subscription and coalesces the changes at each
// Runtime event boundary. Authored changes also emit a scene node event.
class GAMESPROUT_RUNTIME_PUBLIC_TYPE IRuntimeDetailedPropertyObserver : public IRuntimePropertyObserver {
public:
    virtual void OnRuntimeObjectPropertiesChanged(std::uint64_t sceneID, std::uint64_t objectID,
        RuntimePropertyChange changes) = 0;

private:
    void OnRuntimeObjectPropertiesChanged(std::uint64_t, std::uint64_t) final {}
};
}
