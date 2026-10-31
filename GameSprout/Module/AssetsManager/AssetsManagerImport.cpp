#include "AssetsManager.h"
#include "../Shared/AssetArtifactIO.h"
#include <QDateTime>
#include <QElapsedTimer>

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
void AssetsManager::ImportAsset(IAssetMeta * meta, bool force, bool sourceRecorded) {
    if (!meta || _ConflictingGuids.count(meta->_Guid.GetGuidString())) return;
    if (!sourceRecorded && !RecordPendingSource(meta)) return;
    if (meta->_ImporterType == eAssetImporterType::Folder || meta->_ImporterType == eAssetImporterType::Script) return;
    const std::string guid = meta->_Guid.GetGuidString();
    _Pipeline->Invalidate(meta->_Guid);
    const uint64_t operation = ++_NextImportID;
    _ActiveImportCallbacks.insert(operation);
    try {
        _Pipeline->ImportAsset(meta, [guid,operation](const AssetImportResult & result) {
            AssetsManager * owner = s_Active;
            if (!owner || !owner->_ActiveImportCallbacks.count(operation)) return;
            // Finish the startup barrier even when result handling fails or retries.
            struct Completion {
                AssetsManager * Owner;
                uint64_t Operation;
                ~Completion() {
                    Owner->_ActiveImportCallbacks.erase(Operation);
                    if (!Owner->_ReconcileOnScan && Owner->_ActiveImportCallbacks.empty()) Owner->FinishStartupLoading();
                }
            } completion{owner,operation};
            if (result.Superseded) {
                if (result.Retry) owner->ImportAsset(owner->QueryAsset(Guid(guid)));
                return;
            }
            const auto found = owner->_Assets.find(guid);
            if (found == owner->_Assets.end()) return;
            if (!result.Success) {
                owner->RecordImportResult(found->second, result);
                GameSproutConsoleLog(g_IConsole,eConsoleLog::Error,"Import {} failed: {}",found->second->GetPath(),result.Error);
                return;
            }
            IAssetMeta * current = found->second;
            if (current->GetPath() != result.Manifest.SourcePath) return;
            std::string before, after;
            current->SerializeMeta(before);
            if (!current->SetSubAssets(result.Manifest.SubAssetsJson) || !current->SerializeMeta(after)) return;
            if (after != before && !current->SaveMeta()) {
                AssetImportResult failed = result;
                failed.Success = false;
                failed.Error = "Cannot persist imported identities";
                owner->RecordImportResult(current, failed);
                GameSproutConsoleLog(g_IConsole,eConsoleLog::Error,"Cannot persist imported identities: {}",current->GetPath());
                return;
            }
            owner->RecordImportResult(current, result);
            if(!result.Diagnostics.empty())
                GameSproutConsoleLog(g_IConsole,eConsoleLog::Warning,"Import diagnostics for {}:\n{}",current->GetPath(),result.Diagnostics);
            if (after != before) owner->ImportDependents(current->GetPath());
            GameSproutConsoleLog(g_IConsole,eConsoleLog::Trace,"{} {}: {} resources",
                result.Cached ? "缓存命中" : "预处理完成", current->GetPath(), result.Manifest.Assets.size());
        }, force);
    } catch (...) {
        // SubmitTask can reject synchronously; its callback already balances the count.
        // Exceptions before callback ownership is transferred need the same cleanup.
        _ActiveImportCallbacks.erase(operation);
        if (!_ReconcileOnScan && _ActiveImportCallbacks.empty()) FinishStartupLoading();
        throw;
    }
}
bool AssetsManager::ReimportAsset(const Guid & guid) {
    IAssetMeta * meta = QueryAsset(guid);
    if (!meta || meta->_ImporterType == eAssetImporterType::Folder || meta->_ImporterType == eAssetImporterType::Script) return false;
    if (!meta->SaveMeta()) return false;
    ImportAsset(meta, true);
    ImportDependents(meta->GetPath());
    return true;
}
bool AssetsManager::ApplyImportSettings(const Guid & guid, const std::string & settingsJson,
    const std::string & expectedSettingsJson, std::string & error) {
    error.clear();
    auto * meta = static_cast<AssetMeta *>(QueryAsset(guid));
    if (!meta || !_SourceDatabaseReady || _ConflictingGuids.count(guid.GetGuidString())) {
        error = "资源不存在、数据库尚未就绪或 GUID 冲突。";
        return false;
    }
    Json::Value candidate, expected, current;
    std::string snapshot;
    if (!AssetArtifactIO::Parse(settingsJson, candidate) || !candidate.isObject()
        || !AssetArtifactIO::Parse(expectedSettingsJson, expected) || !expected.isObject()
        || !meta->SerializeMeta(snapshot) || !AssetArtifactIO::Parse(snapshot, current)) {
        error = "导入设置快照无效。";
        return false;
    }
    if (current["ImportSettings"] != expected) {
        error = "导入设置已被外部修改，请恢复到最新设置后重新编辑。";
        return false;
    }
    if (candidate == expected) return true;
    if (meta->_ImporterType == eAssetImporterType::Folder || meta->_ImporterType == eAssetImporterType::Script
        || !meta->ApplySettings(candidate)) {
        error = "设置校验或 Meta 保存失败，原设置已保留。";
        return false;
    }
    ImportAsset(meta, true);
    ImportDependents(meta->GetPath());
    return true;
}
void AssetsManager::ImportDependents(const std::string & relativePath) {
    // Transitive invalidation, bounded by a visited set for cyclic resource graphs.
    std::deque<std::string> paths{relativePath};
    std::set<std::string> visited{relativePath};
    while (!paths.empty()) {
        const std::string path = paths.front(); paths.pop_front();
        auto dependents=_Library->FindDependents(path);
        const auto declarations=_Pipeline->FindSourceDependents(path);
        dependents.insert(dependents.end(),declarations.begin(),declarations.end());
        for (const std::string & owner : dependents) {
            const auto asset = _Assets.find(owner);
            if (asset != _Assets.end() && visited.insert(asset->second->GetPath()).second) {
                ImportAsset(asset->second);
                paths.push_back(asset->second->GetPath());
            }
        }
    }
}

void AssetsManager::Update(GameSproutModuleApi *) {
    DispatchAssetEvents();
}

void AssetsManager::WatcherDir(const std::string & path) {
    const std::string canonical=QFileInfo(QString::fromStdString(path)).canonicalFilePath().toStdString();
    if (!s_Active || canonical.empty() || !s_Active->_Watches.insert(canonical).second) return;
    if (!g_GameSproutModuleApi->_Foundation->SystemApi()->FileSystemWatcher(canonical,s_Active,OnFileSystemWatcher)) {
        s_Active->_Watches.erase(canonical);
        GameSproutConsoleLog(g_IConsole,eConsoleLog::Warning,"Cannot watch directory: {}",path);
    }
}

void AssetsManager::ScanDirectory(const std::string & path) {
    if (!_SourceDatabaseReady) return;
    QElapsedTimer elapsed;
    elapsed.start();
    std::vector<FoundationUtils::SafeSystem::File::FileInfo> infos;
    FoundationUtils::SafeSystem::File::EnumerateDirectory(path, "*", infos, true, {}, {"meta", "META"});
    if (!QFileInfo(QString::fromStdString(path)).isReadable()) _ScanFailed = true;
    const auto fileInfo = [](const FoundationUtils::SafeSystem::File::FileInfo & info) {
        const std::string filename = info._Name + ((!info._IsDirectory && !info._Externsion.empty()) ? "." + info._Externsion : "");
        return QFileInfo(QDir(QString::fromStdString(info._ParentDirectory)).filePath(QString::fromStdString(filename)));
    };
    // Enumerate once. Register every identity before submitting native documents
    // that may refer to a source appearing later in this same enumeration.
    for (const auto & info : infos) {
        if (!info._IsDirectory && ToLower(info._Externsion) == "meta") continue;
        const QFileInfo file = fileInfo(info);
        if (!file.exists() || file.isSymLink()) continue;
        Foundation::FileSystemWatcherEvent event;
        event.Type = info._IsDirectory ? Foundation::eWatcherType::Directory : Foundation::eWatcherType::File;
        event.Action = Foundation::eFileSystemAction::Create;
        event.Path = file.absoluteFilePath().toStdString();
        RestoreMissingMeta(event.Path);
        if (info._IsDirectory) {
            if (!file.isReadable()) {
                _ScanFailed = true;
                GameSproutConsoleLog(g_IConsole,eConsoleLog::Error,"Cannot read asset directory: {}",event.Path);
            }
            WatcherDir(event.Path);
        }
        const eAssetImporterType type = info._IsDirectory ? eAssetImporterType::Folder : GetAssetImporterType(info._Externsion);
        IAssetMeta * meta = TriggerFactory(type,event,info._Name,info._Externsion,this);
        if (!meta && type != eAssetImporterType::Unknown) {
            RecordSourceError(RelativePath(event.Path), "Cannot load source Meta");
            GameSproutConsoleLog(g_IConsole,eConsoleLog::Error,"Cannot load source Meta: {}",event.Path);
        }
        StoreAssetMeta(meta);
    }
    // All comparisons and submissions finish before this function returns.
    // Update() never drains an import queue or starts a second scan.
    struct ImportCandidate { IAssetMeta * Meta; };
    std::vector<ImportCandidate> imports;
    std::vector<AssetSourceRecord> records;
    size_t reused = 0;
    for (const auto & info : infos) {
        if (!info._IsDirectory && ToLower(info._Externsion) == "meta") continue;
        const QFileInfo file = fileInfo(info);
        if (!file.exists() || file.isSymLink()) continue;
        const auto known = _AssetGuidsByPath.find(RelativePath(file.absoluteFilePath().toStdString()));
        if (known == _AssetGuidsByPath.end()) continue;
        IAssetMeta * meta = QueryAsset(Guid(known->second));
        if (!meta) continue;
        if (_ConflictingGuids.count(known->second)) {
            AssetSourceRecord failed = DescribeSource(meta);
            failed.Status = "Failed";failed.Error = "Duplicate asset GUID";
            records.push_back(std::move(failed));
            continue;
        }
        const auto previous = _SourceRecords.find(known->second);
        AssetSourceRecord current = DescribeSource(meta);
        const bool fileInfoChanged = previous != _SourceRecords.end()
            && (previous->second.SourceSize != current.SourceSize
                || previous->second.SourceModifyTime != current.SourceModifyTime
                || previous->second.MetaModifyTime != current.MetaModifyTime);
        if (previous != _SourceRecords.end() && previous->second.Path != current.Path && !meta->SaveMeta()) {
            RecordSourceError(previous->second.Path, "Cannot update moved Meta");
            _ScanFailed = true;
            continue;
        }
        if (meta->_ImporterType == eAssetImporterType::Folder || meta->_ImporterType == eAssetImporterType::Script) {
            records.push_back(std::move(current));
            continue;
        }
        if (!fileInfoChanged && previous != _SourceRecords.end() && previous->second.Status == "Ready"
            && previous->second.Path == current.Path && previous->second.Importer == current.Importer
            && previous->second.MetaJson == current.MetaJson && previous->second.SettingsHash == current.SettingsHash
            && _Pipeline->CanReuseAsset(meta)) {
            ++reused;
            continue;
        }
        // 移动资源可能刚写回 Meta 的相对路径，批量保存前重新取得最终观察值。
        if (previous != _SourceRecords.end() && previous->second.Path != current.Path) current = DescribeSource(meta);
        current.SourceHash.clear();
        records.push_back(std::move(current));
        imports.push_back({meta});
    }
    // 一次同步事务登记本次真正需要处理的资源，提交完成后才启动工作线程。
    // imports 仅存在于本次调用中，不由 Update 延迟派发。
    if (!SaveSourceRecords(records)) { _ScanFailed = true; return; }
    const uint64_t batch = _Pipeline->BeginImportBatch(imports.size());
    struct BatchRegistration {
        IAssetsPipeline * Pipeline;
        uint64_t Batch;
        ~BatchRegistration() { Pipeline->EndImportBatch(Batch); }
    } registration{_Pipeline, batch};
    for (const auto & candidate : imports) {
        // 时间/身份或失败状态只表示需要完整验证，是否重建由内容、设置和产物校验决定。
        // 用户显式 ReimportAsset 仍单独传 force=true，启动扫描不覆盖这条语义。
        ImportAsset(candidate.Meta, false, true);
        if (!_ReconcileOnScan) ImportDependents(candidate.Meta->GetPath());
    }
    GameSproutConsoleLog(g_IConsole, eConsoleLog::Trace,
        "Assets scan: {} reused, {} imports submitted, {} ms", reused, imports.size(), elapsed.elapsed());
}
