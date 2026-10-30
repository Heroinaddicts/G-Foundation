#include "LibraryManager.h"
#include "../Shared/AssetArtifactIO.h"
#include <QUuid>

namespace {
bool ValidJournal(const Json::Value & journal) {
    if (!journal.isObject() || journal["Version"] != 1 || !journal["Guid"].isString()
        || !Guid(journal["Guid"].asString()).IsValid() || !journal["Generation"].isString()
        || !journal["Files"].isArray()) return false;
    std::set<std::string> names;
    for (const Json::Value & file : journal["Files"]) {
        if (!file["LocalID"].isUInt64() || file["LocalID"].asUInt64() == 0 || !file["HadOld"].isBool()
            || !file["Name"].isString()) return false;
        const std::string expected = journal["Guid"].asString() + "_" + std::to_string(file["LocalID"].asUInt64()) + ".gsasset";
        if (file["Name"] != expected || !names.insert(expected).second) return false;
    }
    return true;
}

bool RollbackFiles(const QDir & library, const QDir & transaction, const Json::Value & journal) {
    bool restored = true;
    for (const Json::Value & file : journal["Files"]) {
        const QString name = QString::fromStdString(file["Name"].asString());
        const QString target = library.filePath(name);
        const QString saved = transaction.filePath(name + ".previous");
        if (QFileInfo::exists(saved)) {
            if (QFileInfo::exists(target) && !QFile::remove(target)) { restored = false; continue; }
            if (!QFile::rename(saved, target)) restored = false;
        } else if (!file["HadOld"].asBool() && QFileInfo::exists(target)) {
            if (!QFile::remove(target)) restored = false;
        }
    }
    return restored;
}

LDBWriteOperation JsonWrite(const std::string & table, const std::string & key, const Json::Value & value) {
    const std::string json = AssetArtifactIO::JsonText(value);
    return {table, key, std::vector<uint8_t>(json.begin(), json.end()), false};
}
}

bool LibraryManager::RecoverPublications() {
    const QDir library(QString::fromStdString(_LibraryRoot));
    const QDir transactions(library.filePath(".Transactions"));
    ILDBTable * publications = _LDBSession->GetTable("Publications");
    for (const QFileInfo & item : transactions.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        QDir transaction(item.absoluteFilePath());
        QFile journalFile(transaction.filePath("publication.json"));
        if (!journalFile.exists()) {
            // No final file may be moved until this journal has been written.
            if (!transaction.removeRecursively()) return false;
            continue;
        }
        Json::Value journal;
        if (!journalFile.open(QIODevice::ReadOnly)
            || !AssetArtifactIO::Parse(journalFile.readAll().toStdString(), journal) || !ValidJournal(journal)
            || journal["Generation"] != item.fileName().toStdString()) return false;
        journalFile.close();
        const std::string generation = journal["Generation"].asString();
        if (!publications->IsExists(generation) && !RollbackFiles(library, transaction, journal)) return false;
        if (!transaction.removeRecursively()) return false;
        if (!publications->Delete(generation)) return false;
    }
    return true;
}

bool LibraryManager::PublishFlatImport(const std::string & stage, AssetImportManifest & manifest, std::string & error) {
    if (_PublicationBlocked) { error = "An interrupted publication needs recovery; reopen the project"; return false; }
    const QDir library(QString::fromStdString(_LibraryRoot));
    const QDir staging(QString::fromStdString(stage));
    AssetImportManifest previous;
    const bool hasPrevious = GetManifest(Guid(manifest.GuidString), previous);
    AssetImportManifest published = manifest;
    published.Directory.clear();
    published.Generation = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    Json::Value journal;
    journal["Version"] = 1;
    journal["Guid"] = manifest.GuidString;
    journal["Generation"] = published.Generation;
    journal["Files"] = Json::Value(Json::arrayValue);
    for (AssetArtifact & asset : published.Assets) {
        Json::Value file;
        file["LocalID"] = Json::UInt64(asset.LocalID);
        asset.Path = manifest.GuidString + "_" + std::to_string(asset.LocalID) + ".gsasset";
        file["Name"] = asset.Path;
        const QFileInfo old(library.filePath(QString::fromStdString(asset.Path)));
        if (old.isSymLink() || (old.exists() && !old.isFile())) { error = "Invalid artifact destination"; return false; }
        file["HadOld"] = old.exists();
        journal["Files"].append(file);
    }
    const Json::Value encoded = AssetArtifactIO::ToJson(published);
    AssetImportManifest checked;
    if (!AssetArtifactIO::FromJson(encoded, checked) || !ValidJournal(journal)) { error = "Invalid flat artifact manifest"; return false; }
    std::vector<LDBWriteOperation> operations{JsonWrite("Manifests", manifest.GuidString, encoded)};
    if (!BuildIndexOperations(hasPrevious ? &previous : nullptr, &published, operations)) { error = "Invalid dependency index"; return false; }
    // This marker commits with the indexes. It also makes recovery safe if cleanup
    // fails and the same source is imported or deleted again before the next startup.
    operations.push_back({"Publications", published.Generation, {1}, false});
    QDir transaction(library.filePath(".Transactions/" + QString::fromStdString(published.Generation)));
    if (!QDir().rename(staging.absolutePath(), transaction.absolutePath())) { error = "Cannot prepare artifact publication"; return false; }
    if (!AssetArtifactIO::Write(transaction.filePath("publication.json"), QByteArray::fromStdString(AssetArtifactIO::JsonText(journal)), error)) {
        transaction.removeRecursively();
        return false;
    }
    bool replaced = true;
    for (Json::ArrayIndex index = 0; index < journal["Files"].size(); ++index) {
        const Json::Value & file = journal["Files"][index];
        const QString name = QString::fromStdString(file["Name"].asString());
        const QString target = library.filePath(name);
        if (file["HadOld"].asBool() && !QFile::rename(target, transaction.filePath(name + ".previous"))) { replaced = false; break; }
        const QString source = transaction.filePath(QString::fromStdString(manifest.Assets[index].Path));
        if (!QFile::rename(source, target)) { replaced = false; break; }
    }
    if (!replaced || !_LDBSession->WriteBatch(operations)) {
        if (RollbackFiles(library, transaction, journal)) transaction.removeRecursively();
        else _PublicationBlocked = true; // Keep recovery files; the job no longer owns this directory.
        error = "Cannot publish artifacts; previous data was restored or retained for recovery";
        return false;
    }
    manifest = std::move(published);
    if (hasPrevious && previous.Directory.empty()) {
        std::set<uint64_t> active;
        for (const AssetArtifact & asset : manifest.Assets) active.insert(asset.LocalID);
        for (const AssetArtifact & old : previous.Assets) if (!active.count(old.LocalID)) {
            const std::string expected = previous.GuidString + "_" + std::to_string(old.LocalID) + ".gsasset";
            if (old.Path == expected) QFile::remove(library.filePath(QString::fromStdString(expected)));
        }
    }
    ClearLoaded(manifest.GuidString);
    if (transaction.removeRecursively()) _LDBSession->GetTable("Publications")->Delete(manifest.Generation);
    return true;
}
