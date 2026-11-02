#pragma once
#include "GameSproutEditorApi.h"

enum class EditorPlayState : uint8_t { Edit, EnteringPlay, Playing, Paused, ExitingPlay };

struct PlayModeState {
    EditorPlayState State = EditorPlayState::Edit;
    std::string Error;
};

class IPlayModeListener {
public:
    virtual ~IPlayModeListener() = default;
    // Host thread, borrowed snapshot. Subscribe before reading the initial state.
    virtual void OnPlayModeStateChanged(const PlayModeState&) = 0;
};

// Editor execution-session coordinator. Runtime and GRHI do not depend on this interface.
class IPlayMode : public GameSproutEditor::IGameSproutModule {
public:
    virtual PlayModeState GetState() const = 0;
    // Play may enter EnteringPlay while the latest project sources compile.
    virtual bool Play(std::string& error) = 0;
    virtual bool Pause(std::string& error) = 0;
    virtual bool Resume(std::string& error) = 0;
    // Queue exactly one gameplay frame, retaining Paused afterwards.
    virtual bool Step(std::string& error) = 0;
    virtual bool Stop(std::string& error) = 0;
    virtual bool RegisterPlayModeListener(IPlayModeListener*) = 0;
    virtual void UnregisterPlayModeListener(IPlayModeListener*) = 0;
};
