#ifndef __AssetsManager_h__
#define __AssetsManager_h__

#include "Header.h"

class AssetsManager : public IAssetsManager {
public:
    virtual ~AssetsManager() {}

    IAssetMeta * QueryAsset(const Guid & guid) const override;

    typedef IAssetMeta*(*fOnAssetFactory)(const Foundation::FileSystemWatcherEvent &, const std::string &, const std::string &, void * const);
    static void RegistFactory(const eAssetType type, const fOnAssetFactory & callback)  {
        auto it = s_AssetFactorys.find(type);
        if (s_AssetFactorys.end() == it) {
            s_AssetFactorys.insert({type, callback});
        } else {
            Q_ASSERT(false);
        }
    }

    static IAssetMeta* TriggerFactory(const eAssetType type, const Foundation::FileSystemWatcherEvent &event, const std::string& name, const std::string& extension, void * const context) {
        auto it = s_AssetFactorys.find(type);
        if (s_AssetFactorys.end() != it) {
            return it->second(event, name, extension, context);
        }

        return nullptr;
    }

private:
    bool Initialize(GameSproutApi * const api) override;
    bool Launch(GameSproutApi * const api) override;
    void LaunchFinished(GameSproutApi * const api) override;
    void Update(GameSproutApi * const api) override;
    void Release(GameSproutApi * const api) override;

    static void WatcherDir(const std::string & path);
    void ScanDirectory(const std::string & path);
    void StoreAssetMeta(IAssetMeta * const meta);
    void RemoveAssetRecords(const std::string & path, const bool recursive);
    static void OnFileSystemWatcher(const Foundation::FileSystemWatcherEvent & event, const std::string & name, const std::string & extension, void * const context);

    static void OnFileCreate(const Foundation::FileSystemWatcherEvent & event, const std::string & name, const std::string & extension, void * const context);
    static void OnFileDelete(const Foundation::FileSystemWatcherEvent & event, const std::string & name, const std::string & extension, void * const context);
    static void OnFileModify(const Foundation::FileSystemWatcherEvent & event, const std::string & name, const std::string & extension, void * const context);
    static void OnFileRename(const Foundation::FileSystemWatcherEvent & event, const std::string & name, const std::string & extension, void * const context);

    static void OnFolderCreate(const Foundation::FileSystemWatcherEvent & event, const std::string & name, const std::string & extension, void * const context);
    static void OnFolderDelete(const Foundation::FileSystemWatcherEvent & event, const std::string & name, const std::string & extension, void * const context);
    static void OnFolderModify(const Foundation::FileSystemWatcherEvent & event, const std::string & name, const std::string & extension, void * const context);
    static void OnFolderRename(const Foundation::FileSystemWatcherEvent & event, const std::string & name, const std::string & extension, void * const context);

private:
    std::string _AssetsRoot;
    std::unordered_map<std::string, IAssetMeta *> _Assets;
    std::unordered_map<std::string, std::string> _AssetGuidsByPath;

    inline static std::map<eAssetType, fOnAssetFactory> s_AssetFactorys;
    inline static AssetsManager * s_Active = nullptr;
};

#endif //__AssetsManager_h__
