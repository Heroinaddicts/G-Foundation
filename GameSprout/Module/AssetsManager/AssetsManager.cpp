#include "AssetsManager.h"

bool AssetsManager::Initialize(GameSproutApi * const api) {
    g_GameSproutApi = api;
    GetGameSproutModule(api->_EditorApi, g_IProject, Project);
    GetGameSproutModule(api->_EditorApi, g_IConsole, Console);

    GSRegLaunchedCallback(
        g_IProject,
        [&](GameSproutApi * const api) -> bool {
            if (api->_Foundation->GetLaunchArg("IsNewProject")) {
                QDir dir(QString::fromStdString(g_IProject->GetProjectRoot()));
                this->_AssetsRoot = QString::fromStdString(g_IProject->GetProjectRoot() + "/Assets");
                GameSproutConsoleLog(g_IConsole, eConsoleLog::Trace, "New Project Create Assets Dir {}", this->_AssetsRoot.toStdString());
                return dir.mkdir("Assets");
            }

            this->_AssetsRoot = QString::fromStdString(g_IProject->GetProjectRoot() + "/Assets");
            GameSproutConsoleLog(g_IConsole, eConsoleLog::Trace, "Project Assets Dir {}", this->_AssetsRoot.toStdString());
            QDir dir(this->_AssetsRoot);
            return dir.exists();
        }
    );

    return true;
}

bool AssetsManager::Launch(GameSproutApi * const api)  {
    return true;
}


void AssetsManager::WatcherDir(const std::string & path) {
    g_GameSproutApi->_Foundation->SystemApi()->FileSystemWatcher(
        path,
        nullptr,
        AssetsManager::OnFileSystemWatcher
    );

    std::vector<FoundationUtils::SafeSystem::File::FileInfo> files;
    int count = FoundationUtils::SafeSystem::File::GetFileInDirectory(path, "*", files, false);
    for(int i=0; i<count; i++) {
        if (files[i]._IsDirectory) {
            WatcherDir(files[i]._ParentDirectory + "/" + files[i]._Name);
        }
    }
}

void AssetsManager::LaunchFinished(GameSproutApi * const api)  {
    WatcherDir(_AssetsRoot.toStdString());
}

void AssetsManager::Update(GameSproutApi * const api)  {
}

void AssetsManager::Release(GameSproutApi * const api)  {
}


IAsset * AssetsManager::QueryAsset(const std::string & guid) const {
    auto it = _Assets.find(guid);
    if (_Assets.end() == it) {
        return nullptr;
    }

    return it->second;
}

void AssetsManager::OnFileSystemWatcher(const Foundation::FileSystemWatcherEvent & event, const std::string & extension, void * const context) {
    qInfo() << "eWatcherType:" << (int)event.Type
            << ", eFileSystemAction:" << (int)event.Action
            << ", path " << QString::fromStdString(event.Path)
            << ", oldPath " << QString::fromStdString(event.OldPath)
            << ", extension " << QString::fromStdString(extension);

    switch(event.Action) {
    case eFileSystemAction::CreateDir:
        WatcherDir(event.Path);
        break;
    default:
        break;
    }
}
