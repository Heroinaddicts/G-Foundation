#pragma once
#include "GameSproutEditorApi.h"
#include "RuntimeProjectSettings.h"

class IProjectSettingsListener {
public:
    virtual ~IProjectSettingsListener() = default;
    // Host thread, borrowed value snapshot; copy it to retain it.
    virtual void OnProjectSettingsChanged(const GameSproutRuntime::RuntimeProjectSettings&, std::uint64_t revision) = 0;
};

// Project definitions and file transactions. Runtime receives only value data.
class IProjectSettings : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~IProjectSettings() = default;
    virtual GameSproutRuntime::RuntimeProjectSettings GetSettings() const = 0;
    virtual std::uint64_t GetRevision() const = 0;
    // Atomic save with optimistic concurrency. Rejects Play, callbacks, reused
    // Tag IDs, invalid masks and removal of referenced tags. No implicit remap.
    virtual bool SaveSettings(const GameSproutRuntime::RuntimeProjectSettings&,
        std::uint64_t expectedRevision, std::string& error) = 0;
    // Subscribe before reading the initial snapshot. Caller unregisters first.
    virtual bool RegisterSettingsListener(IProjectSettingsListener*) = 0;
    virtual void UnregisterSettingsListener(IProjectSettingsListener*) = 0;
};
