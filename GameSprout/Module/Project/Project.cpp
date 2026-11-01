#include "Project.h"
#include <QDebug>

bool Project::Initialize(GameSproutModuleApi * const api) {
    g_GameSproutModuleApi = api;
    const char* projectRoot = api->_Foundation->GetLaunchArg("ProjectRoot");
    if (!projectRoot || !*projectRoot) {
        qCritical() << "Missing project directory. Launch with --ProjectRoot=<project directory>.";
        return false;
    }
    _Root = projectRoot;

    GetGameSproutModule(api->_EditorApi, g_IConsole, Console);
    GSRegLaunchedCallback(
        g_IConsole,
        [&](GameSproutModuleApi * const api) {
            GameSproutConsoleLog(g_IConsole, eConsoleLog::Debug, "{}", _Root);
            return true;
        }
    );

    return true;
}

bool Project::Launch(GameSproutModuleApi * const api)  {
    return true;
}

void Project::LaunchFinished(GameSproutModuleApi * const api)  {

}

void Project::Update(GameSproutModuleApi * const api)  {
}

void Project::Release(GameSproutModuleApi * const api)  {
    
}

const std::string & Project::GetProjectRoot() const {
    return _Root;
}
