#include "LibraryManager.h"
#include <QDir>

void LibraryManager::BeforeInitialize(GameSproutApi * const api) {
    g_Self = this;
    GetGameSproutModule(api->_EditorApi, g_IProject, Project);
    GetGameSproutModule(api->_EditorApi, g_IConsole, Console);
    GetGameSproutModule(api->_EditorApi, g_ILocalDatabase, LocalDatabase);

    GSRegLaunchedCallback(
        g_IProject,
        [&](GameSproutApi * const api) -> bool {
            this->_LibraryRoot = g_IProject->GetProjectRoot() + "/" + LIBRARY_DIR_NAME;

            QDir dir(QString::fromStdString(g_IProject->GetProjectRoot()));
            if (!dir.exists(LIBRARY_DIR_NAME) && !dir.mkdir(LIBRARY_DIR_NAME)) {
                GameSproutConsoleLog(g_IConsole, eConsoleLog::Trace, "Project Open or Create Library {} Dir Failed", this->_LibraryRoot);
                return false;
            }

            _LDBSession = g_ILocalDatabase->OpenLDB(this->_LibraryRoot + "/" + ARTIFACT_DB_FILENAME);
            if (nullptr == _LDBSession) {
                _LDBSession = g_ILocalDatabase->CreateLDB(this->_LibraryRoot + "/" + ARTIFACT_DB_FILENAME);
                if (nullptr == _LDBSession) {
                    GameSproutConsoleLog(g_IConsole, eConsoleLog::Trace, "Project Open or Create Artifact DB Failed");
                    return false;
                }
            }

            return true;
        }
    );
}

bool LibraryManager::Initialize(GameSproutApi * const api) {
    g_GameSproutApi = api;
    return true;
}

bool LibraryManager::Launch(GameSproutApi * const api)  {
    return true;
}

void LibraryManager::LaunchFinished(GameSproutApi * const api)  {

}

void LibraryManager::Update(GameSproutApi * const api)  {
    
}

void LibraryManager::Release(GameSproutApi * const api)  {
    
}
