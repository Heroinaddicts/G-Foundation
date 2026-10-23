#include "Project.h"

bool Project::Initialize(GameSproutApi * const api) {
    g_GameSproutApi = api;
    _Root = api->_Foundation->GetLaunchArg("ProjectRoot");

    GetGameSproutModule(api->_EditorApi, g_IConsole, Console);
    GSRegLaunchedCallback(
        g_IConsole,
        [&](GameSproutApi * const api) {
            GameSproutConsoleLog(g_IConsole, eConsoleLog::Debug, "{}", _Root);
            return true;
        }
    );

    return true;
}

bool Project::Launch(GameSproutApi * const api)  {
    return true;
}

void Project::LaunchFinished(GameSproutApi * const api)  {

}

void Project::Update(GameSproutApi * const api)  {
    
}

void Project::Release(GameSproutApi * const api)  {
    
}

const std::string & Project::GetProjectRoot() const {
    return _Root;
}
