#include "Console.h"

bool Console::Initialize(GameSproutApi * const api) {
    g_GameSproutApi = api;
    return true;
}

bool Console::Launch(GameSproutApi * const api)  {
    GetGameSproutView(api->_EditorApi, g_IConsoleUI, ConsoleUI);
    return true;
}

void Console::LaunchFinished(GameSproutApi * const api)  {

}

void Console::Update(GameSproutApi * const api)  {
    
}

void Console::Release(GameSproutApi * const api)  {
    
}

void Console::ConsoleLog(const char * file, const int line, const eConsoleLog e, const std::string & log) {
    g_IConsoleUI->ConsoleLog(file, line, e, log);
}
