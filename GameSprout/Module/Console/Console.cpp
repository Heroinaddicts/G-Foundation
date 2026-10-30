#include "Console.h"

bool Console::Initialize(GameSproutModuleApi * const api) {
    g_GameSproutModuleApi = api;
    return true;
}

bool Console::Launch(GameSproutModuleApi * const api)  {
    GetGameSproutView(api->_EditorApi, g_IConsoleUI, ConsoleUI);
    return true;
}

void Console::LaunchFinished(GameSproutModuleApi * const api)  {

}

void Console::Update(GameSproutModuleApi * const api)  {
    
}

void Console::Release(GameSproutModuleApi * const api)  {
    
}

void Console::ConsoleLog(const char * file, const int line, const eConsoleLog e, const std::string & log) {
    g_IConsoleUI->ConsoleLog(file, line, e, log);
}
