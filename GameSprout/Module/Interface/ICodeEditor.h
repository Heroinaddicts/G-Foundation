#pragma once
#include "GameSproutEditorApi.h"

struct CodeEditorState {
    bool Preparing = false;
    std::string ProjectPath;
    std::string Error;
};
class ICodeEditorListener {
public:
    virtual ~ICodeEditorListener() = default;
    // Host thread, borrowed snapshot. Register before reading the initial state.
    virtual void OnCodeEditorStateChanged(const CodeEditorState&) = 0;
};
// External source-code workspace service. Platform adapters choose the IDE;
// resource browsers and component inspectors never launch IDE processes directly.
class ICodeEditor : public GameSproutEditor::IGameSproutModule {
public:
    // Assets-relative native source path. True means an asynchronous request was
    // accepted; preparation/launch errors are published through the state event.
    virtual bool OpenSource(const std::string& relativePath,std::string& error) = 0;
    virtual CodeEditorState GetState() const { return {}; }
    virtual bool RegisterCodeEditorListener(ICodeEditorListener*) { return false; }
    virtual void UnregisterCodeEditorListener(ICodeEditorListener*) {}
};
