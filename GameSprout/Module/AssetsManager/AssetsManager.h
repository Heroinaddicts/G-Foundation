#ifndef __AssetsManager_h__
#define __AssetsManager_h__

#include "Header.h"
#include "IAssetsPipeline.h"
#include "ILibraryManager.h"
#include "AssetSourceRecord.h"
#include <QDateTime>
#include <deque>
#include <set>
#include <chrono>

class AssetsManager : public IAssetsManager {
public:
    virtual ~AssetsManager() {}

    IAssetMeta * QueryAsset(const Guid & guid) const override;
    bool ReimportAsset(const Guid & guid) override;
    std::vector<IAssetMeta *> GetAssetMetas() const override;
    std::vector<IAssetMeta *> GetAssetsInDirectory(const std::string & path) const override;
    bool RegisterAssetListener(IAssetListener * listener) override;
    void UnregisterAssetListener(IAssetListener * listener) override;

    typedef IAssetMeta*(*fOnAssetFactory)(const Foundation::FileSystemWatcherEvent &, const std::string &, const std::string &, void * const);
    static void RegistFactory(const eAssetImporterType type, const fOnAssetFactory & callback)  {
        auto it = s_AssetFactorys.find(type);
        if (s_AssetFactorys.end() == it) {
            s_AssetFactorys.insert({type, callback});
        } else {
            Q_ASSERT(false);
        }
    }

    static IAssetMeta* TriggerFactory(const eAssetImporterType type, const Foundation::FileSystemWatcherEvent &event, const std::string& name, const std::string& extension, void * const context) {
        auto it = s_AssetFactorys.find(type);
        if (s_AssetFactorys.end() != it) {
            return it->second(event, name, extension, context);
        }

        return nullptr;
    }

private:
    friend class AssetMeta;
    void QueueAssetEvent(eAssetMetaEvent action, const AssetSourceRecord & record, const std::string & oldPath = {});
    void DispatchAssetEvents();
    std::map<uint64_t, IAssetListener *> _AssetListeners;
    uint64_t _NextListener = 0;
    std::deque<AssetEvent> _AssetEvents;
    bool _DispatchingEvents = false;
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
    void QueueImport(IAssetMeta * meta);
    void QueueDependents(const std::string & relativePath);
    void ReloadMeta(const std::string & sourcePath);
    std::string RelativePath(const std::string & absolutePath) const;
    void RenameSource(const Foundation::FileSystemWatcherEvent & event, bool directory);
    bool OpenSourceDatabase();
    bool SaveSourceRecord(const AssetSourceRecord & record);
    bool SaveSourceRecords(const std::vector<AssetSourceRecord> & records);
    void ForgetSourceRecord(const std::string & guid);
    bool ArchiveSourceRecord(const std::string & guid, const std::string & reason);
    void CheckDatabaseSources();
    AssetSourceRecord DescribeSource(IAssetMeta * meta) const;
    void RecordPendingSource(IAssetMeta * meta);
    void RecordImportResult(IAssetMeta * meta, const AssetImportResult & result);
    void RecordSourceError(const std::string & path, const std::string & error);
    void RestoreMissingMeta(const std::string & path);
    void ReconcileSources();
    void FinishStartupLoading();
    std::set<std::string> _Watches;
    std::set<std::string> _DirectoriesToScan;
    std::map<std::string, std::chrono::steady_clock::time_point> _Dirty;
    std::set<std::string> _Importing;
    std::set<std::string> _ForceImports;
    std::set<std::string> _ConflictingGuids;
    IAssetsPipeline * _Pipeline = nullptr;
    ILibraryManager * _Library = nullptr;
    bool _ReconcileOnScan = true;
    bool _ScanFailed = false;
    bool _StartupWatchesVerified = false;
    bool _StartupLoading = false;
    bool _SourceDatabaseReady = false;
    std::map<std::string, AssetSourceRecord> _SourceRecords;
    std::unordered_map<std::string, std::set<std::string>> _SourceGuidsByPath;
    std::map<std::string, std::map<std::string, AssetSourceRecord>> _BackupSourcesByPath;
    std::map<std::string, AssetSourceRecord> _StartupSourceRecords;
    std::string _AssetsRoot;
    std::string _CanonicalAssetsRoot;
    std::unordered_map<std::string, IAssetMeta *> _Assets;
    std::unordered_map<std::string, std::string> _AssetGuidsByPath;

    inline static std::map<eAssetImporterType, fOnAssetFactory> s_AssetFactorys;
    inline static AssetsManager * s_Active = nullptr;
};

#endif //__AssetsManager_h__
