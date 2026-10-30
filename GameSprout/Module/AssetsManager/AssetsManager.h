#ifndef __AssetsManager_h__
#define __AssetsManager_h__

#include "Header.h"
#include "IAssetsPipeline.h"
#include "ILibraryManager.h"
#include "AssetSourceRecord.h"
#include <QDateTime>
#include <deque>
#include <set>

class AssetsManager : public IAssetsManager {
public:
    virtual ~AssetsManager() {}

    IAssetMeta * QueryAsset(const Guid & guid) const override;
    bool ReimportAsset(const Guid & guid) override;
    bool ApplyImportSettings(const Guid & guid, const std::string & settingsJson,
        const std::string & expectedSettingsJson, std::string & error) override;
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
    bool Initialize(GameSproutModuleApi * const api) override;
    bool Launch(GameSproutModuleApi * const api) override;
    void LaunchFinished(GameSproutModuleApi * const api) override;
    void Update(GameSproutModuleApi * const api) override;
    void Release(GameSproutModuleApi * const api) override;

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
    void ImportAsset(IAssetMeta * meta, bool force = false);
    void ImportDependents(const std::string & relativePath);
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
    bool RecordPendingSource(IAssetMeta * meta);
    void RecordImportResult(IAssetMeta * meta, const AssetImportResult & result);
    void RecordSourceError(const std::string & path, const std::string & error);
    void RestoreMissingMeta(const std::string & path);
    void ReconcileSources();
    void FinishStartupLoading();
    std::set<std::string> _Watches;
    std::set<std::string> _ConflictingGuids;
    IAssetsPipeline * _Pipeline = nullptr;
    ILibraryManager * _Library = nullptr;
    bool _ReconcileOnScan = true;
    bool _ScanFailed = false;
    bool _StartupLoading = false;
    uint64_t _NextImportID = 0;
    std::set<uint64_t> _ActiveImportCallbacks; // Submitted operations only; never a scheduling queue.
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
