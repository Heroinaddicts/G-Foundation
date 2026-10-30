#ifndef __IConsoleUI_h__
#define __IConsoleUI_h__

#include "GameSproutEditorApi.h"

enum class eConsoleLog : uint8_t {
    Debug,
    Trace,
    Warning,
    Error
};

class IConsoleUI : public GameSproutEditor::IGameSproutView {
public:
    virtual ~IConsoleUI() {}
    // UTF-8 plain text. After Initialize, worker-thread calls are dispatched
    // to the UI thread; the caller must keep the view alive during the call.
    virtual void ConsoleLog(const char * file, const int line, const eConsoleLog e, const std::string & log) = 0;
};

#endif //__IConsoleUI_h__
