#include "AssetsManager.h"
#include "../Shared/AssetArtifactIO.h"
#include <QDateTime>

std::vector<IAssetMeta *> AssetsManager::GetAssetMetas() const {
    std::vector<IAssetMeta *> result;
    for (const auto & entry : _Assets) result.push_back(entry.second);
    return result;
}
std::string AssetsManager::RelativePath(const std::string & absolutePath) const {
    const QDir directory(QString::fromStdString(_AssetsRoot));
    const QString root = directory.canonicalPath().isEmpty() ? QString::fromStdString(_CanonicalAssetsRoot) : directory.canonicalPath();
    if (root.isEmpty()) return "";
    QString path = QDir::cleanPath(QDir::fromNativeSeparators(QString::fromStdString(absolutePath)));
    const QString lexicalRoot = directory.absolutePath();
    if (path.startsWith(lexicalRoot + "/")) path = root + path.mid(lexicalRoot.size());
    return QDir(root).relativeFilePath(path).toStdString();
}
void AssetsManager::QueueImport(IAssetMeta * meta) {
    if (!meta || _ConflictingGuids.count(meta->_Guid.GetGuidString())) return;
    RecordPendingSource(meta);
    if (meta->_ImporterType == eAssetImporterType::Folder || meta->_ImporterType == eAssetImporterType::Script) return;
    _Pipeline->Invalidate(meta->_Guid);
    _Dirty[meta->_Guid.GetGuidString()] = std::chrono::steady_clock::now();
}
bool AssetsManager::ReimportAsset(const Guid & guid) {
    IAssetMeta * meta = QueryAsset(guid);
    if (!meta || meta->_ImporterType == eAssetImporterType::Folder || meta->_ImporterType == eAssetImporterType::Script) return false;
    if (!meta->SaveMeta()) return false;
    _ForceImports.insert(guid.GetGuidString());
    QueueImport(meta);
    QueueDependents(meta->GetPath());
    return true;
}
void AssetsManager::QueueDependents(const std::string & relativePath) {
    // Transitive invalidation, bounded by a visited set for cyclic resource graphs.
    std::deque<std::string> paths{relativePath};
    std::set<std::string> visited;
    while (!paths.empty()) {
        const std::string path = paths.front(); paths.pop_front();
        if (!visited.insert(path).second) continue;
        for (const std::string & owner : _Library->FindDependents(path)) {
            const auto asset = _Assets.find(owner);
            if (asset != _Assets.end()) { QueueImport(asset->second); paths.push_back(asset->second->GetPath()); }
        }
    }
}

void AssetsManager::Update(GameSproutApi *) {
    if (!_SourceDatabaseReady) return;
    DispatchAssetEvents();
    // Keep filesystem callbacks short. Enumerate/register watches on the next
    // main-thread Update, after the current filesystem event dispatch finishes.
    const auto directories = std::move(_DirectoriesToScan);
    _DirectoriesToScan.clear();
    for (const std::string & path : directories) {
        const QFileInfo directory(QString::fromStdString(path));
        if (!directory.isDir()) continue;
        Foundation::FileSystemWatcherEvent event;
        event.Type = Foundation::eWatcherType::Directory;
        event.Action = Foundation::eFileSystemAction::Create;
        event.Path = path;
        if (RelativePath(path) != ".")
            StoreAssetMeta(TriggerFactory(eAssetImporterType::Folder,event,directory.fileName().toStdString(),"",this));
        WatcherDir(path);
        ScanDirectory(path);
    }
    if (_ReconcileOnScan && !_StartupWatchesVerified && !_ScanFailed) {
        // The first pass installs watches for every discovered directory. Read again
        // after registration so additions in the discovery/registration gap are included.
        // New directories created during this pass are covered by their parent's watch.
        _StartupWatchesVerified = true;
        ScanDirectory(_AssetsRoot);
        return;
    }
    if (_ReconcileOnScan) {
        ReconcileSources();
        _ReconcileOnScan = false;
    }
    const auto now=std::chrono::steady_clock::now();
    int submitted=0;
    for (auto it=_Dirty.begin(); it!=_Dirty.end() && submitted<4 && _Importing.size()<8;) {
        if (_Importing.count(it->first)) { ++it; continue; }
        if (now-it->second<std::chrono::milliseconds(100)) {++it;continue;}
        const std::string guid=it->first;
        it=_Dirty.erase(it);
        const auto found=_Assets.find(guid);
        if (found==_Assets.end()) continue;
        ++submitted;
        _Importing.insert(guid);
        const bool force = _ForceImports.erase(guid) != 0;
        _Pipeline->ImportAsset(found->second,[guid](const AssetImportResult & result) {
            AssetsManager * owner=s_Active;
            if (!owner) return;
            owner->_Importing.erase(guid);
            if (result.Superseded) {
                if (result.Retry) {
                    IAssetMeta * current=owner->QueryAsset(Guid(guid));
                    if (current) owner->QueueImport(current);
                }
                return;
            }
            const auto found=owner->_Assets.find(guid);
            if (found==owner->_Assets.end()) return;
            if (!result.Success) {
                owner->RecordImportResult(found->second, result);
                GameSproutConsoleLog(g_IConsole,eConsoleLog::Error,"Import {} failed: {}",found->second->GetPath(),result.Error);
                return;
            }
            IAssetMeta * meta=found->second;
            if (meta->GetPath()!=result.Manifest.SourcePath) return;
            // Updating only the identity registry preserves current settings and Meta ownership.
            std::string before,after;
            meta->SerializeMeta(before);
            if (!meta->SetSubAssets(result.Manifest.SubAssetsJson) || !meta->SerializeMeta(after)) return;
            if (after!=before && !meta->SaveMeta()) {
                AssetImportResult failed = result;
                failed.Success = false;
                failed.Error = "Cannot persist imported identities";
                owner->RecordImportResult(meta, failed);
                GameSproutConsoleLog(g_IConsole,eConsoleLog::Error,"Cannot persist imported identities: {}",meta->GetPath());
                return;
            }
            owner->RecordImportResult(meta, result);
            if (after != before) owner->QueueDependents(meta->GetPath());
            GameSproutConsoleLog(g_IConsole,eConsoleLog::Trace,"Imported {}: {} resources{}",meta->GetPath(),result.Manifest.Assets.size(),result.Cached?" (cached)":"");
        },force);
    }
    if (!_ReconcileOnScan && _DirectoriesToScan.empty() && _Dirty.empty() && _Importing.empty())
        FinishStartupLoading();
    DispatchAssetEvents();
}

void AssetsManager::WatcherDir(const std::string & path) {
    if (!s_Active || !s_Active->_Watches.insert(path).second) return;
    if (!g_GameSproutApi->_Foundation->SystemApi()->FileSystemWatcher(path,s_Active,OnFileSystemWatcher)) {
        s_Active->_Watches.erase(path);
        GameSproutConsoleLog(g_IConsole,eConsoleLog::Warning,"Cannot watch directory: {}",path);
    }
}

void AssetsManager::ScanDirectory(const std::string & path) {
    if (!_SourceDatabaseReady) return;
    std::vector<FoundationUtils::SafeSystem::File::FileInfo> infos;
    FoundationUtils::SafeSystem::File::EnumerateDirectory(path, "*", infos, true, {}, {"meta", "META"});
    if (!QFileInfo(QString::fromStdString(path)).isReadable()) _ScanFailed = true;
    for (const auto & info : infos) {
        if (!info._IsDirectory && ToLower(info._Externsion) == "meta") continue;
        const std::string filename = info._Name + ((!info._IsDirectory && !info._Externsion.empty()) ? "." + info._Externsion : "");
        const QFileInfo file(QDir(QString::fromStdString(info._ParentDirectory)).filePath(QString::fromStdString(filename)));
        if (!file.exists() || file.isSymLink()) continue;
        Foundation::FileSystemWatcherEvent event;
        event.Type = info._IsDirectory ? Foundation::eWatcherType::Directory : Foundation::eWatcherType::File;
        event.Action = Foundation::eFileSystemAction::Create;
        event.Path = file.absoluteFilePath().toStdString();
        // Each discovered resource is handled here on the main thread. These calls
        // recover/create Meta, register its GUID and check the database immediately.
        // Imports queue until the full GUID registry is available for dependencies.
        RestoreMissingMeta(event.Path);
        if (info._IsDirectory) {
            if (!file.isReadable()) {
                _ScanFailed = true;
                GameSproutConsoleLog(g_IConsole,eConsoleLog::Error,"Cannot read asset directory: {}",event.Path);
            }
            WatcherDir(event.Path);
            StoreAssetMeta(TriggerFactory(eAssetImporterType::Folder,event,info._Name,"",this));
        } else OnFileCreate(event,info._Name,info._Externsion,this);
        const auto known = _AssetGuidsByPath.find(RelativePath(event.Path));
        if (known == _AssetGuidsByPath.end() || _ConflictingGuids.count(known->second)) continue;
        IAssetMeta * meta = QueryAsset(Guid(known->second));
        if (!meta) continue;
        const AssetSourceRecord current = DescribeSource(meta);
        const auto previous = _SourceRecords.find(known->second);
        if (meta->_ImporterType != eAssetImporterType::Folder && meta->_ImporterType != eAssetImporterType::Script
            && previous != _SourceRecords.end()
            && (previous->second.SourceSize != current.SourceSize
                || previous->second.SourceModifyTime != current.SourceModifyTime
                || previous->second.MetaModifyTime != current.MetaModifyTime
                || previous->second.Status == "Failed"))
            _ForceImports.insert(known->second);
        if (!_ReconcileOnScan) continue; // Live handlers have already persisted Pending.
        // Persist each observation during enumeration, while retaining the pre-scan
        // snapshot for move/deletion reconciliation and dependency invalidation.
        const auto original = _StartupSourceRecords.find(known->second);
        if (original == _StartupSourceRecords.end() || original->second.Path == current.Path)
            SaveSourceRecord(current);
    }
}
