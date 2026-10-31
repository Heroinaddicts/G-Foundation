#include "AssetsManager.h"
#include "../Shared/AssetMetaFormat.h"
#include <filesystem>
#include <QUuid>

namespace {
bool SourceIsMissing(const QString & path) {
    std::error_code error;
    const std::string utf8 = path.toStdString();
    const auto status = std::filesystem::symlink_status(std::filesystem::u8path(utf8), error);
    // Permission/I/O errors are not evidence that a source has been deleted.
    return error == std::errc::no_such_file_or_directory
        || (!error && status.type() == std::filesystem::file_type::not_found);
}
}

bool AssetsManager::OpenSourceDatabase() {
    ILDBSession * database = _Library->GetLDBSession();
    if (!database || !database->CreateTable("AssetSources") || !database->CreateTable("AssetSourceBackups")) return false;
    std::map<std::string, AssetSourceRecord> records;
    bool valid = true;
    const bool read = database->GetTable("AssetSources")->ForEach(
        [&](const std::string & key, const void * data, int length) {
            Json::Value value;
            AssetSourceRecord record;
            if (!AssetArtifactIO::Parse(std::string(static_cast<const char *>(data), size_t(length)), value)
                || !AssetSourceRecord::FromJson(value, record) || record.GuidString != key) {
                valid = false;
                return false;
            }
            records.emplace(key, std::move(record));
            return true;
        });
    if (!read || !valid) return false;

    std::map<std::string, int64_t> backupTimes;
    _BackupSourcesByPath.clear();
    bool backupsValid = true;
    const bool backupsRead = database->GetTable("AssetSourceBackups")->ForEach(
        [&](const std::string &, const void * data, int length) {
            Json::Value value; AssetSourceRecord record;
            if (!AssetArtifactIO::Parse(std::string(static_cast<const char *>(data), size_t(length)), value)
                || !value.isObject() || value["Version"] != 1
                || !value["BackupTime"].isInt64() || !AssetSourceRecord::FromJson(value["Record"], record)) {
                backupsValid = false; return false;
            }
            const std::string key = record.GuidString + ":" + record.Path;
            const int64_t time = value["BackupTime"].asInt64();
            if (!backupTimes.count(key) || backupTimes[key] <= time) {
                backupTimes[key] = time;
                _BackupSourcesByPath[record.Path][record.GuidString] = record;
            }
            return true;
        });
    if (!backupsRead || !backupsValid) return false;
    _SourceRecords = std::move(records);
    _SourceGuidsByPath.clear();
    for (const auto & entry : _SourceRecords) _SourceGuidsByPath[entry.second.Path].insert(entry.first);
    _SourceDatabaseReady = true;
    return true;
}

AssetSourceRecord AssetsManager::DescribeSource(IAssetMeta * meta) const {
    AssetSourceRecord record;
    record.GuidString = meta->_Guid.GetGuidString();
    record.Path = meta->GetPath();
    record.Importer = meta->_ImporterType;
    const auto previous = _SourceRecords.find(record.GuidString);
    if (previous != _SourceRecords.end()) record.SourceHash = previous->second.SourceHash;
    const QDir root(QString::fromStdString(_AssetsRoot));
    const QFileInfo source(root.filePath(QString::fromStdString(record.Path)));
    const QFileInfo sidecar(source.absoluteFilePath() + ".meta");
    record.SourceSize = source.exists() ? source.size() : -1;
    record.SourceModifyTime = source.lastModified().isValid() ? source.lastModified().toMSecsSinceEpoch() : -1;
    record.MetaModifyTime = sidecar.lastModified().isValid() ? sidecar.lastModified().toMSecsSinceEpoch() : -1;
    Json::Value json;
    if (meta->SerializeMeta(record.MetaJson) && AssetArtifactIO::Parse(record.MetaJson, json)) {
        record.SettingsHash = AssetArtifactIO::Hash(QByteArray::fromStdString(
            std::to_string(static_cast<uint16_t>(record.Importer)) + ":" + AssetArtifactIO::JsonText(json["ImportSettings"])));
    }
    if (record.Importer == eAssetImporterType::Folder || record.Importer == eAssetImporterType::Script)
        record.Status = "NotRequired";
    return record;
}

bool AssetsManager::SaveSourceRecord(const AssetSourceRecord & record) {
    return SaveSourceRecords({record});
}

bool AssetsManager::SaveSourceRecords(const std::vector<AssetSourceRecord> & records) {
    if (!_SourceDatabaseReady) return false;
    std::vector<LDBWriteOperation> operations;
    for (const AssetSourceRecord & record : records) {
        const auto previous = _SourceRecords.find(record.GuidString);
        const std::string json = AssetArtifactIO::JsonText(record.ToJson());
        if (previous != _SourceRecords.end() && AssetArtifactIO::JsonText(previous->second.ToJson()) == json) continue;
        operations.push_back({"AssetSources", record.GuidString, std::vector<uint8_t>(json.begin(), json.end()), false});
    }
    if (operations.empty()) return true;
    if (!_Library->GetLDBSession()->WriteBatch(operations)) {
        GameSproutConsoleLog(g_IConsole, eConsoleLog::Error, "Cannot commit asset source records");
        return false;
    }
    for (const AssetSourceRecord & record : records) {
        const auto previous = _SourceRecords.find(record.GuidString);
        if (previous != _SourceRecords.end() && previous->second.Path != record.Path) {
            auto oldPath = _SourceGuidsByPath.find(previous->second.Path);
            if (oldPath != _SourceGuidsByPath.end()) {
                oldPath->second.erase(record.GuidString);
                if (oldPath->second.empty()) _SourceGuidsByPath.erase(oldPath);
            }
        }
        const bool created = previous == _SourceRecords.end();
        const std::string oldPath = created ? "" : previous->second.Path;
        const bool changed = created || previous->second.ToJson() != record.ToJson();
        _SourceRecords[record.GuidString] = record;
        _SourceGuidsByPath[record.Path].insert(record.GuidString);
        if (changed) QueueAssetEvent(created ? eAssetMetaEvent::Create : eAssetMetaEvent::Modify, record, oldPath == record.Path ? "" : oldPath);
    }
    return true;
}

void AssetsManager::ForgetSourceRecord(const std::string & guid) {
    const auto previous = _SourceRecords.find(guid);
    if (previous != _SourceRecords.end()) {
        auto path = _SourceGuidsByPath.find(previous->second.Path);
        if (path != _SourceGuidsByPath.end()) {
            path->second.erase(guid);
            if (path->second.empty()) _SourceGuidsByPath.erase(path);
        }
        _SourceRecords.erase(previous);
    }
}

bool AssetsManager::RecordPendingSource(IAssetMeta * meta) {
    if (!_SourceDatabaseReady || !meta) return false;
    AssetSourceRecord record = DescribeSource(meta);
    // A watcher reports a possible change, not its new content fingerprint.
    if (record.Status != "NotRequired") record.SourceHash.clear();
    if (SaveSourceRecord(record)) return true;
    _ScanFailed = true;
    return false;
}

void AssetsManager::RecordImportResult(IAssetMeta * meta, const AssetImportResult & result) {
    if (!_SourceDatabaseReady) return;
    AssetSourceRecord record = DescribeSource(meta);
    if (result.Manifest.SourcePath == record.Path && result.Manifest.SettingsHash == record.SettingsHash)
        record.SourceHash = result.Manifest.SourceHash;
    record.Status = result.Success ? "Ready" : "Failed";
    record.Error = result.Error;
    SaveSourceRecord(record);
}

void AssetsManager::RecordSourceError(const std::string & path, const std::string & error) {
    // Preserve the last valid Meta and good Library generation when a sidecar is broken.
    std::vector<AssetSourceRecord> failed;
    const auto known = _SourceGuidsByPath.find(path);
    if (known != _SourceGuidsByPath.end()) for (const std::string & guid : known->second)
        failed.push_back(_SourceRecords.at(guid));
    for (AssetSourceRecord & record : failed) {
        _Pipeline->Invalidate(Guid(record.GuidString));
        record.Status = "Failed";
        record.Error = error;
        SaveSourceRecord(record);
    }
}

void AssetsManager::RestoreMissingMeta(const std::string & path) {
    if (!_SourceDatabaseReady || QFileInfo::exists(QString::fromStdString(path + ".meta"))) return;
    const std::string relative = RelativePath(path);
    const AssetSourceRecord * previous = nullptr;
    const auto known = _SourceGuidsByPath.find(relative);
    if (known != _SourceGuidsByPath.end()) {
        if (known->second.size() != 1) return;
        previous = &_SourceRecords.at(*known->second.begin());
    } else {
        const auto backup = _BackupSourcesByPath.find(relative);
        if (backup == _BackupSourcesByPath.end() || backup->second.size() != 1) return;
        previous = &backup->second.begin()->second;
        // A GUID currently attached to another resource cannot be restored by path.
        const auto active = _SourceRecords.find(previous->GuidString);
        if (active != _SourceRecords.end() && active->second.Path != relative) return;
    }
    if (previous->MetaJson.empty()) return;
    Json::Value meta;
    std::string error;
    if (!AssetArtifactIO::Parse(previous->MetaJson, meta) || !AssetMetaFormat::Validate(meta, error)
        || meta["Guid"] != previous->GuidString) return;
    const std::string content = AssetArtifactIO::JsonText(meta);
    QFile file(QString::fromStdString(path + ".meta"));
    // Create only: an external tool may have written a sidecar since the existence check.
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return;
    if (file.write(content.data(), qint64(content.size())) != qint64(content.size())) {
        file.close();
        QFile::remove(file.fileName());
        GameSproutConsoleLog(g_IConsole, eConsoleLog::Error, "Cannot restore missing Meta: {}", path);
    }
}

void AssetsManager::ReconcileSources() {
    size_t added = 0, changed = 0, moved = 0, removed = 0;
    const QDir root(QString::fromStdString(_AssetsRoot));
    const auto previous = _StartupSourceRecords;
    for (const auto & entry : _Assets) {
        if (_ConflictingGuids.count(entry.first)) continue;
        IAssetMeta * meta = entry.second;
        AssetSourceRecord record = DescribeSource(meta);
        // Content hashing and artifact validation are done by this resource's
        // ImportAsset task; preserve the last known hash until its completion.
        const auto old = previous.find(entry.first);
        if (old == previous.end()) ++added;
        else {
            const bool pathChanged = old->second.Path != record.Path;
            if (pathChanged) ++moved;
            if (pathChanged || old->second.SourceSize != record.SourceSize
                || old->second.SourceModifyTime != record.SourceModifyTime
                || old->second.MetaModifyTime != record.MetaModifyTime || old->second.SettingsHash != record.SettingsHash) {
                ++changed;
            }
        }
    }
    // Each scanned source has already been persisted/submitted synchronously.

    // Pruning is permitted only after a complete successful directory scan. An unreadable
    // subtree, corrupt Meta or GUID collision cannot be mistaken for a deleted resource.
    if (!_ScanFailed) for (const auto & entry : previous) {
        if (_Assets.count(entry.first)) continue;
        const auto replacement = _AssetGuidsByPath.find(entry.second.Path);
        const bool replaced = replacement != _AssetGuidsByPath.end() && replacement->second != entry.first;
        if (!replaced && !SourceIsMissing(root.filePath(QString::fromStdString(entry.second.Path)))) {
            RecordSourceError(entry.second.Path, "Source exists but its Meta cannot be loaded");
            continue;
        }
        std::string error;
        if (ArchiveSourceRecord(entry.first, replaced ? "Replaced" : "Deleted")) ++removed;
    }
    _StartupSourceRecords.clear();
    GameSproutConsoleLog(g_IConsole, eConsoleLog::Trace,
        "Assets database comparison: {} added, {} changed ({} moved), {} removed{}",
        added, changed, moved, removed, _ScanFailed ? " (scan incomplete; deletion skipped)" : "");
}

void AssetsManager::CheckDatabaseSources() {
    // ForEach has finished before this method runs. All DB and Meta decisions stay
    // on the main thread; removal remains deferred until GUID matching is complete.
    const QDir root(QString::fromStdString(_AssetsRoot));
    for (const auto & entry : _SourceRecords) {
        const AssetSourceRecord & record = entry.second;
        const QFileInfo source(root.filePath(QString::fromStdString(record.Path)));
        if (!source.exists() || source.isSymLink()) continue;
        RestoreMissingMeta(source.absoluteFilePath().toStdString());
    }
}

bool AssetsManager::ArchiveSourceRecord(const std::string & guid, const std::string & reason) {
    AssetSourceRecord record;
    const auto found = _SourceRecords.find(guid);
    if (found != _SourceRecords.end()) record = found->second;
    else {
        const auto asset = _Assets.find(guid);
        if (asset == _Assets.end()) return false;
        record = DescribeSource(asset->second);
    }
    Json::Value backup;
    backup["Version"] = 1;
    backup["BackupTime"] = Json::Int64(QDateTime::currentMSecsSinceEpoch());
    backup["Reason"] = reason;
    backup["Record"] = record.ToJson();
    AssetImportManifest manifest;
    if (_Library->GetManifest(Guid(guid), manifest)) backup["Manifest"] = AssetArtifactIO::ToJson(manifest);
    const std::string json = AssetArtifactIO::JsonText(backup);
    const std::string key = guid + "/" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const std::vector<LDBWriteOperation> writes{
        {"AssetSourceBackups", key, std::vector<uint8_t>(json.begin(), json.end()), false},
        {"AssetSources", guid, {}, true}
    };
    std::string error;
    if (!_Library->RemoveAsset(Guid(guid), error, writes)) {
        GameSproutConsoleLog(g_IConsole, eConsoleLog::Error, "Cannot archive deleted source {}: {}", record.Path, error);
        return false;
    }
    _BackupSourcesByPath[record.Path][guid] = record;
    ForgetSourceRecord(guid);
    QueueAssetEvent(eAssetMetaEvent::Remove, record);
    // File removal is deliberately after the durable transaction. A crash may leave
    // an orphan Meta, but cannot destroy the only recovery copy before it is backed up.
    const QString path = QDir(QString::fromStdString(_AssetsRoot)).filePath(QString::fromStdString(record.Path));
    if (SourceIsMissing(path)) {
        QFile sidecar(path + ".meta"); Json::Value value; std::string sidecarError;
        if (sidecar.open(QIODevice::ReadOnly) && AssetArtifactIO::Parse(sidecar.readAll().toStdString(), value)
            && AssetMetaFormat::Validate(value, sidecarError) && value["Guid"] == guid) {
            sidecar.close();
            if (!QFile::remove(path + ".meta")) GameSproutConsoleLog(g_IConsole, eConsoleLog::Warning, "Cannot remove orphan Meta: {}", path.toStdString());
        }
    }
    return true;
}
