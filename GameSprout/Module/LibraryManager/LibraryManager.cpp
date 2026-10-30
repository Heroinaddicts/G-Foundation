#include "LibraryManager.h"
#include "../Shared/AssetArtifactIO.h"
#include <QUuid>
#include <set>

namespace {
class ImportedAsset final : public IAsset {
public:
    ImportedAsset(const Guid & guid, const AssetArtifact & artifact, std::string path)
        : _Guid(guid), _Artifact(artifact), _Path(std::move(path)) {}
    const Guid & GetGuid() const override { return _Guid; }
    uint64_t GetLocalID() const override { return _Artifact.LocalID; }
    eAssetType GetAssetType() const override { return _Artifact.Type; }
    const std::string & GetName() const override { return _Artifact.Name; }
    const std::string & GetArtifactPath() const override { return _Path; }
private:
    const Guid _Guid;
    const AssetArtifact _Artifact;
    const std::string _Path;
};

LDBWriteOperation Put(const std::string & table, const std::string & key, const Json::Value & value) {
    const std::string text = AssetArtifactIO::JsonText(value);
    return {table, key, std::vector<uint8_t>(text.begin(), text.end()), false};
}
}

void LibraryManager::BeforeInitialize(GameSproutApi * const api) {
    g_Self = this;
    _PublicationBlocked = false;
    _Tasks = api->_Foundation->TaskApi();
    GetGameSproutView(api->_EditorApi, _IDE, IDEWindow);
    _StartupTime = QDateTime::currentDateTimeUtc();
    GetGameSproutModule(api->_EditorApi, g_IProject, Project);
    GetGameSproutModule(api->_EditorApi, g_IConsole, Console);
    GetGameSproutModule(api->_EditorApi, g_ILocalDatabase, LocalDatabase);
    GSRegLaunchedCallback(g_IProject, [this](GameSproutApi *) -> bool {
        _LibraryRoot = QDir(QString::fromStdString(g_IProject->GetProjectRoot())).filePath("Library").toStdString();
        QDir directory(QString::fromStdString(_LibraryRoot));
        if (!directory.mkpath(".") || !directory.mkpath(".Transactions") || !directory.mkpath("Staging")) return false;
        _Lock = new QLockFile(directory.filePath("Editor.lock"));
        if (!_Lock->tryLock(0)) {
            GameSproutConsoleLog(g_IConsole, eConsoleLog::Error, "Library is already in use: {}", _LibraryRoot);
            return false;
        }
        const std::string databasePath = _LibraryRoot + "/ArtifactDB";
        // Never reinterpret a corrupt existing database as a new project database.
        _LDBSession = QFileInfo::exists(QString::fromStdString(databasePath + "/data.mdb"))
            ? g_ILocalDatabase->OpenLDB(databasePath) : g_ILocalDatabase->CreateLDB(databasePath);
        return _LDBSession && _LDBSession->CreateTable("Manifests") && _LDBSession->CreateTable("Dependents")
            && _LDBSession->CreateTable("Publications") && RecoverPublications();
    });
}

bool LibraryManager::Initialize(GameSproutApi * const api) { g_GameSproutApi = api; return true; }
bool LibraryManager::Launch(GameSproutApi *) {
    if (!_Tasks || !_IDE) return false;
    _TaskOwner = _Tasks->RegisterTaskOwner();
    return _TaskOwner != Foundation::InvalidTaskOwnerId;
}
void LibraryManager::LaunchFinished(GameSproutApi *) { StartGarbageCollection(); }
void LibraryManager::Update(GameSproutApi *) {}

void LibraryManager::ClearLoaded(const std::string & guid) {
    const auto found = _Loaded.find(guid);
    if (found == _Loaded.end()) return;
    for (IAsset * asset : found->second) delete asset;
    _Loaded.erase(found);
}

void LibraryManager::Release(GameSproutApi *) {
    if (_Tasks && _TaskOwner != Foundation::InvalidTaskOwnerId) _Tasks->UnregisterTaskOwner(_TaskOwner);
    _TaskOwner = Foundation::InvalidTaskOwnerId;
    while (_CleanupTasks > 0) { --_CleanupTasks; if (_IDE) _IDE->Unloading(); }
    while (!_Loaded.empty()) ClearLoaded(_Loaded.begin()->first);
    delete _LDBSession;
    _LDBSession = nullptr;
    delete _Lock; _Lock = nullptr;
    _LibraryRoot.clear();
}

bool LibraryManager::GetManifest(const Guid & guid, AssetImportManifest & manifest) const {
    if (!_LDBSession || !guid.IsValid()) return false;
    ILDBTable * table = _LDBSession->GetTable("Manifests");
    std::vector<uint8_t> bytes;
    Json::Value value;
    return table && table->Get(guid.GetGuidString(), bytes)
        && AssetArtifactIO::Parse(std::string(bytes.begin(), bytes.end()), value)
        && AssetArtifactIO::FromJson(value, manifest) && manifest.GuidString == guid.GetGuidString();
}

bool LibraryManager::BuildIndexOperations(const AssetImportManifest * oldManifest,
    const AssetImportManifest * newManifest, std::vector<LDBWriteOperation> & operations) {
    std::set<std::string> paths;
    if (oldManifest) for (const auto & dependency : oldManifest->Dependencies) paths.insert(dependency.Path);
    if (newManifest) for (const auto & dependency : newManifest->Dependencies) paths.insert(dependency.Path);
    const std::string guid = newManifest ? newManifest->GuidString : oldManifest->GuidString;
    for (const std::string & path : paths) {
        // LMDB keys are bounded; hash the path, and store/validate its full value.
        const std::string key = AssetArtifactIO::Hash(QByteArray::fromStdString(path));
        ILDBTable * table = _LDBSession->GetTable("Dependents");
        std::vector<uint8_t> bytes;
        Json::Value value(Json::objectValue);
        std::set<std::string> owners;
        if (table->Get(key, bytes)) {
            if (!AssetArtifactIO::Parse(std::string(bytes.begin(), bytes.end()), value)
                || value["Path"] != path || !value["Guids"].isArray()) return false;
            for (const Json::Value & owner : value["Guids"]) {
                if (!owner.isString()) return false;
                owners.insert(owner.asString());
            }
        }
        owners.erase(guid);
        if (newManifest) for (const auto & dependency : newManifest->Dependencies) {
            if (dependency.Path == path) owners.insert(guid);
        }
        if (owners.empty()) operations.push_back({"Dependents", key, {}, true});
        else {
            value["Path"] = path;
            value["Guids"] = Json::Value(Json::arrayValue);
            for (const auto & owner : owners) value["Guids"].append(owner);
            operations.push_back(Put("Dependents", key, value));
        }
    }
    return true;
}

bool LibraryManager::CommitImport(const std::string & stage, AssetImportManifest & manifest, std::string & error) {
    if (!_LDBSession || !Guid(manifest.GuidString).IsValid() || !AssetArtifactIO::RelativePath(manifest.SourcePath)) {
        error = "Library is unavailable or manifest identity is invalid"; return false;
    }
    const QString root = QDir(QString::fromStdString(_LibraryRoot)).canonicalPath();
    const QString stagePath = QDir(QString::fromStdString(stage)).canonicalPath();
    if (root.isEmpty() || !stagePath.startsWith(root + "/Staging/") || QFileInfo(QString::fromStdString(stage)).isSymLink()) {
        error = "Import stage is outside this Library"; return false;
    }
    std::set<uint64_t> ids;
    for (const AssetArtifact & asset : manifest.Assets) {
        const QFileInfo file(QDir(stagePath).filePath(QString::fromStdString(asset.Path)));
        if (asset.LocalID == 0 || !ids.insert(asset.LocalID).second || !AssetArtifactIO::RelativePath(asset.Path)
            || !file.isFile() || file.isSymLink() || !file.canonicalFilePath().startsWith(stagePath + "/")) {
            error = "Incomplete or invalid artifact: " + asset.Path; return false;
        }
    }
    return PublishFlatImport(stage, manifest, error);
}

bool LibraryManager::RemoveAsset(const Guid & guid, std::string & error, const std::vector<LDBWriteOperation> & additionalWrites) {
    if (!_LDBSession) { error = "Library unavailable"; return false; }
    AssetImportManifest previous;
    // Source backup/removal and the Library indexes publish in one main-thread transaction.
    std::vector<LDBWriteOperation> operations = additionalWrites;
    operations.push_back({"Manifests", guid.GetGuidString(), {}, true});
    if (GetManifest(guid, previous) && !BuildIndexOperations(&previous, nullptr, operations)) {
        error = "Invalid dependency index"; return false;
    }
    if (!_LDBSession->WriteBatch(operations)) { error = "Cannot remove artifact index"; return false; }
    ClearLoaded(guid.GetGuidString());
    return true;
}

std::vector<std::string> LibraryManager::FindDependents(const std::string & sourcePath) const {
    std::vector<std::string> owners;
    if (!_LDBSession) return owners;
    ILDBTable * table = _LDBSession->GetTable("Dependents");
    std::vector<uint8_t> bytes;
    Json::Value value;
    if (table && table->Get(AssetArtifactIO::Hash(QByteArray::fromStdString(sourcePath)), bytes)
        && AssetArtifactIO::Parse(std::string(bytes.begin(), bytes.end()), value)
        && value["Path"] == sourcePath && value["Guids"].isArray()) {
        for (const Json::Value & owner : value["Guids"]) if (owner.isString()) owners.push_back(owner.asString());
    }
    return owners;
}

IAsset * LibraryManager::QueryAsset(const Guid & guid, uint64_t localID) {
    const std::string key = guid.GetGuidString();
    auto found = _Loaded.find(key);
    if (found == _Loaded.end()) {
        AssetImportManifest manifest;
        if (!GetManifest(guid, manifest)) return nullptr;
        std::vector<IAsset *> assets;
        assets.reserve(manifest.Assets.size());
        try {
            for (const AssetArtifact & artifact : manifest.Assets) {
                assets.push_back(new ImportedAsset(guid, artifact, _LibraryRoot + "/" + manifest.Directory + "/" + artifact.Path));
            }
            found = _Loaded.emplace(key, std::move(assets)).first;
        } catch (...) { for (IAsset * asset : assets) delete asset; throw; }
    }
    for (IAsset * asset : found->second) if (asset->GetLocalID() == localID) return asset;
    return nullptr;
}

std::vector<std::string> LibraryManager::GetManifestGuids() const {
    std::vector<std::string> result;
    if (_LDBSession) _LDBSession->GetTable("Manifests")->ForEach([&](const std::string & key,const void *,int) {
        result.push_back(key);return true;
    });
    return result;
}
