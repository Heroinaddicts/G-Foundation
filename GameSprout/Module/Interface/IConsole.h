#ifndef __IConsole_h__
#define __IConsole_h__

#include "GameSproutEditorApi.h"
#include "IConsoleUI.h"
#include "GRHIApi.h"
#include <format>

class IConsole : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~IConsole() {}

    virtual void ConsoleLog(const char * file, const int line, const eConsoleLog e, const std::string & log) = 0;
};

#define GameSproutConsoleLog(console, e, ...) do { \
        IConsole* const gameSproutConsoleTarget = (console); \
        if (gameSproutConsoleTarget != nullptr) { \
            gameSproutConsoleTarget->ConsoleLog(__FILE__, __LINE__, (e), std::format(__VA_ARGS__)); \
    } \
} while (false)

#endif //__IConsole_h__
