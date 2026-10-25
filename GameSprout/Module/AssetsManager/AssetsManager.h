#ifndef __AssetsManager_h__
#define __AssetsManager_h__

#include "Header.h"

class AssetsManager : public IAssetsManager {
public:
    virtual ~AssetsManager() {}

    IAsset * QueryAsset(const IGuid * const guid) const override;

private:
    bool Initialize(GameSproutApi * const api) override;
    bool Launch(GameSproutApi * const api) override;
    void LaunchFinished(GameSproutApi * const api) override;
    void Update(GameSproutApi * const api) override;
    void Release(GameSproutApi * const api) override;

    static void WatcherDir(const std::string & path);
    static void OnFileSystemWatcher(const Foundation::FileSystemWatcherEvent & event, const std::string & extension, void * const context);

    static void OnFileCreate(const Foundation::FileSystemWatcherEvent & event, const std::string & extension, void * const context);
    static void OnFileDelete(const Foundation::FileSystemWatcherEvent & event, const std::string & extension, void * const context);
    static void OnFileModify(const Foundation::FileSystemWatcherEvent & event, const std::string & extension, void * const context);
    static void OnFileRename(const Foundation::FileSystemWatcherEvent & event, const std::string & extension, void * const context);

    static void OnFolderCreate(const Foundation::FileSystemWatcherEvent & event, const std::string & extension, void * const context);
    static void OnFolderDelete(const Foundation::FileSystemWatcherEvent & event, const std::string & extension, void * const context);
    static void OnFolderModify(const Foundation::FileSystemWatcherEvent & event, const std::string & extension, void * const context);
    static void OnFolderRename(const Foundation::FileSystemWatcherEvent & event, const std::string & extension, void * const context);

private:
    std::string _AssetsRoot;
    std::unordered_map<std::string, IAsset *> _Assets;
};

#endif //__AssetsManager_h__
