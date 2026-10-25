#include "AssetsManager.h"
#include "TextureAssetFactory.hpp"

bool AssetsManager::Initialize(GameSproutApi * const api) {
    g_GameSproutApi = api;
    GetGameSproutModule(api->_EditorApi, g_IProject, Project);
    GetGameSproutModule(api->_EditorApi, g_IConsole, Console);
    GetGameSproutView(api->_EditorApi, g_IIDEWindow, IDEWindow);

    GSRegLaunchedCallback(
        g_IProject,
        [&](GameSproutApi * const api) -> bool {
            if (api->_Foundation->GetLaunchArg("IsNewProject")) {
                QDir dir(QString::fromStdString(g_IProject->GetProjectRoot()));
                this->_AssetsRoot = QString::fromStdString(g_IProject->GetProjectRoot() + "/Assets").toStdString();
                GameSproutConsoleLog(g_IConsole, eConsoleLog::Trace, "New Project Create Assets Dir {}", this->_AssetsRoot);
                return dir.mkdir("Assets");
            }

            this->_AssetsRoot = QString::fromStdString(g_IProject->GetProjectRoot() + "/Assets").toStdString();
            GameSproutConsoleLog(g_IConsole, eConsoleLog::Trace, "Project Assets Dir {}", this->_AssetsRoot);
            QDir dir(QString::fromStdString(this->_AssetsRoot));
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
    int count = FoundationUtils::SafeSystem::File::EnumerateDirectory(path, "*", files, false);
    for(int i=0; i<count; i++) {
        if (files[i]._IsDirectory) {
            WatcherDir(files[i]._ParentDirectory + "/" + files[i]._Name);
        }
    }
}

void AssetsManager::LaunchFinished(GameSproutApi * const api)  {
    WatcherDir(_AssetsRoot);

    std::vector<FoundationUtils::SafeSystem::File::FileInfo> infos;
    int count = FoundationUtils::SafeSystem::File::EnumerateDirectory(_AssetsRoot, "*", infos, true, {}, {"meta"});
    for(int i=0; i<count; i++) {
        GameSproutConsoleLog(g_IConsole, eConsoleLog::Debug, "Asset : {}/{}{}{}",
            infos[i]._ParentDirectory, infos[i]._Name,
            infos[i]._Externsion.empty() ? "" : ".", infos[i]._Externsion);
    }
}

void AssetsManager::Update(GameSproutApi * const api)  {
}

void AssetsManager::Release(GameSproutApi * const api)  {
}


IAsset * AssetsManager::QueryAsset(const IGuid * const guid) const {
    return nullptr;
}


void AssetsManager::OnFileCreate(const Foundation::FileSystemWatcherEvent & event, const std::string & extension, void * const context) {
}

void AssetsManager::OnFileDelete(const Foundation::FileSystemWatcherEvent & event, const std::string & extension, void * const context) {
}

void AssetsManager::OnFileModify(const Foundation::FileSystemWatcherEvent & event, const std::string & extension, void * const context) {
}

void AssetsManager::OnFolderCreate(const Foundation::FileSystemWatcherEvent & event, const std::string & extension, void * const context) {
}

void AssetsManager::OnFolderDelete(const Foundation::FileSystemWatcherEvent & event, const std::string & extension, void * const context) {
}

void AssetsManager::OnFolderModify(const Foundation::FileSystemWatcherEvent & event, const std::string & extension, void * const context) {

}

void AssetsManager::OnFileSystemWatcher(const Foundation::FileSystemWatcherEvent & event, const std::string & extension, void * const context) {
    qInfo() << event.OldPath << event.Path << (int)event.Type << (int)event.Action;

    switch(event.Action) {
    case eFileSystemAction::CreateDir:
        WatcherDir(event.Path);
        break;
    case Foundation::eFileSystemAction::ModifyDir: {
        break;
    }
    default:
        break;
    }
}
