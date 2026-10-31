#include "AssetImportJob.h"
#include "ModelTextureResolver.h"
#include "AnimationResourceValidation.h"
#include <QUuid>
#include "../Shared/AssetValueBinary.h"
#include "../Shared/AssetMetaFormat.h"
#include "IShaderCompiler.h"

AssetImportJob::~AssetImportJob() {
    if (!Stage.empty()) QDir(QString::fromStdString(Stage)).removeRecursively();
}
bool AssetImportJob::Cancelled() {
    if (Context && Context->IsCancellationRequested()) { Fail("Import cancelled"); return true; }
    return false;
}
bool AssetImportJob::Fail(const std::string & error) { Result.Success = false; Result.Error = error; return false; }

uint64_t AssetImportJob::ID(eAssetType type, const std::string & key, const std::string & name) {
    const std::string identity = std::to_string(static_cast<uint16_t>(type)) + ":" + key;
    const auto found = IDs.find(identity);
    if (found != IDs.end()) return found->second;
    // Deterministic recovery after Library deletion, with persisted collision resolution.
    uint64_t id = 0;
    for (uint64_t salt = 0;; ++salt) {
        const QByteArray hash = QCryptographicHash::hash(QByteArray::fromStdString(identity + ":" + std::to_string(salt)), QCryptographicHash::Sha256);
        id = 0;
        for (int i = 0; i < 8; ++i) id = (id << 8) | static_cast<unsigned char>(hash[i]);
        if (id != 0 && UsedIDs.insert(id).second) break;
    }
    IDs.emplace(identity, id);
    Json::Value entry;
    entry["LocalID"] = static_cast<Json::UInt64>(id);
    entry["Type"] = static_cast<uint16_t>(type);
    entry["SourceKey"] = key;
    entry["Name"] = name;
    Identities.append(entry);
    return id;
}

bool AssetImportJob::Emit(eAssetType type, const std::string & key, const std::string & name,
    Json::Value metadata, const QByteArray & payload) {
    if (Context && Context->IsCancellationRequested()) return Fail("Import cancelled");
    const std::string typedKey = std::to_string(static_cast<uint16_t>(type)) + ":" + key;
    if (!ProducedKeys.insert(typedKey).second) return Fail("Duplicate imported object identity: " + key);
    const uint64_t id = ID(type, key, name);
    const bool structured = type == eAssetType::AnimatorController || type == eAssetType::Model || type == eAssetType::Skeleton
        || type == eAssetType::Avatar || type == eAssetType::AvatarMask || type == eAssetType::Material
        || metadata["Format"] == "GameSproutDocument";
    QByteArray bytes;
    Json::Value descriptor;
    if (structured) {
        if (!payload.isEmpty() || !AssetValueBinary::Encode(metadata, bytes))
            return Fail("Cannot encode structural resource data");
        descriptor["Encoding"] = "BinaryValueLE1";
        descriptor["Format"] = metadata.get("Format", "StructuredResource");
        for (const char * key : {"Nodes", "Bones", "Animations", "Textures"})
            if (metadata[key].isArray()) descriptor[std::string(key) + "Count"] = metadata[key].size();
    } else {
        bytes = payload;
        descriptor = std::move(metadata);
        descriptor["Encoding"] = "RawBytes";
    }
    // Resource type, identity, layout and byte size belong to ArtifactDB. The
    // file contains only raw payload or the structural binary body, no JSON header.
    const std::string filename = std::to_string(id) + ".gsasset";
    if (!AssetArtifactIO::Write(QDir(QString::fromStdString(Stage)).filePath(QString::fromStdString(filename)), bytes, Result.Error)) return false;
    AssetArtifact artifact{id, type, name, key, filename, AssetArtifactIO::Hash(bytes)};
    artifact.DataVersion = 2;
    artifact.DataSize = static_cast<uint64_t>(bytes.size());
    artifact.DescriptorJson = AssetArtifactIO::JsonText(descriptor);
    Result.Manifest.Assets.push_back(std::move(artifact));
    return true;
}

bool AssetImportJob::ReadFile(const std::string & path, QByteArray * bytes, std::string & hash) {
    AssetFileFingerprint fingerprint;
    bool changed = false;
    if (!Request.Fingerprints->Read(path, bytes, fingerprint, Result.Error, changed, Context)) {
        if (changed) { Result.Superseded = true; Result.Retry = true; }
        return false;
    }
    const auto previous = ReadVersions.find(path);
    if (previous != ReadVersions.end() && previous->second != fingerprint.Version) {
        Result.Superseded = true; Result.Retry = true;
        return Fail("File changed during import: " + path);
    }
    ReadVersions[path] = fingerprint.Version;
    hash = std::move(fingerprint.Hash);
    return true;
}

bool AssetImportJob::AddDependency(const std::string & relativePath, QByteArray * output) {
    if (!AssetArtifactIO::RelativePath(relativePath)) return Fail("Invalid dependency path: " + relativePath);
    const QDir root(QString::fromStdString(Request.AssetsRoot));
    const QFileInfo info(root.filePath(QString::fromStdString(relativePath)));
    if (!info.isFile() || !info.canonicalFilePath().startsWith(root.canonicalPath() + "/")) return Fail("Missing or external dependency: " + relativePath);
    std::string sourceHash;
    if (!ReadFile(info.absoluteFilePath().toStdString(), output, sourceHash)) return false;
    // 依赖由集中维护的源内容 hash 与规范化 Meta 身份组合，不重新读取共享贴图。
    QByteArray fingerprint("GameSprout.Dependency.2");
    fingerprint.append('\0');
    fingerprint.append(QByteArray::fromStdString(sourceHash));
    const QString metaPath = info.absoluteFilePath() + ".meta";
    AssetFileStamp metaVersion;
    if (!ReadAssetFileStamp(metaPath.toStdString(), metaVersion)) return Fail("Cannot inspect dependency Meta: " + relativePath);
    if (metaVersion.Exists) {
        QByteArray metaBytes;
        Json::Value meta;
        std::string metaHash;
        std::string metaError;
        if (!ReadFile(metaPath.toStdString(), &metaBytes, metaHash) || !AssetArtifactIO::Parse(metaBytes.toStdString(), meta)
            || !AssetMetaFormat::Validate(meta, metaError)) return Fail("Invalid dependency Meta: " + relativePath + "; " + metaError);
        Json::Value identity;
        identity["Guid"] = meta["Guid"];
        identity["ImporterType"] = meta["ImporterType"];
        identity["ImportSettings"] = meta["ImportSettings"];
        identity["SubAssets"] = meta["SubAssets"];
        fingerprint.append('\0');
        fingerprint.append(QByteArray::fromStdString(AssetArtifactIO::JsonText(identity)));
    } else {
        const auto previous = ReadVersions.find(metaPath.toStdString());
        if (previous != ReadVersions.end() && previous->second != metaVersion) {
            Result.Superseded = true; Result.Retry = true;
            return Fail("Dependency Meta changed during import: " + relativePath);
        }
        ReadVersions[metaPath.toStdString()] = metaVersion;
    }
    const std::string hash = AssetArtifactIO::Hash(fingerprint);
    for (const auto & existing : Result.Manifest.Dependencies) if (existing.Path == relativePath) {
        if (existing.Hash != hash) { Result.Superseded = true; Result.Retry = true; return Fail("Dependency changed during import: " + relativePath); }
        return true;
    }
    Result.Manifest.Dependencies.push_back({relativePath, hash});
    return true;
}

bool ModelTextureSearchesCurrent(const AssetImportRequest& request,const AssetImportManifest& manifest,
    std::map<std::string, AssetFileStamp> * observations, bool * observationsAvailable) {
    if(request.Importer!=eAssetImporterType::Model)return true;
    std::vector<std::string> materialDirectories;
    for(const auto& dependency:manifest.Dependencies) {
        if(QFileInfo(QString::fromStdString(dependency.Path)).suffix().compare("mtl",Qt::CaseInsensitive)==0)
            materialDirectories.push_back(QFileInfo(QString::fromStdString(dependency.Path)).path().toStdString());
    }
    // 一次完整查找共用目录版本，不能用第二张贴图的新版本覆盖第一张贴图的旧观察。
    std::map<std::string, AssetFileStamp> directories;
    bool inspected = true, changed = false;
    const auto observe = [&](const std::string & path) {
        AssetFileStamp stamp;
        if (!ReadAssetFileStamp(path, stamp)) { inspected = false; return; }
        const auto previous = directories.find(path);
        if (previous != directories.end() && previous->second != stamp) changed = true;
        else directories.emplace(path, stamp);
    };
    for(const auto& search:manifest.TextureSearches) {
        const auto resolved=ResolveModelTexture(request.AssetsRoot,request.SourcePath,search.Filename,search.RelativeFilename,
            materialDirectories, observations ? observe : std::function<void(const std::string&)>{});
        if(resolved.Path!=search.ResolvedPath||resolved.Reason!=search.Reason)return false;
    }
    if (observations) {
        if (changed) return false;
        if (!inspected && observationsAvailable) *observationsAvailable = false;
        for (const auto & directory : directories) {
            AssetFileStamp current;
            if (!ReadAssetFileStamp(directory.first, current)) {
                if (observationsAvailable) *observationsAvailable = false;
                continue;
            }
            if (current != directory.second) return false;
        }
        *observations = std::move(directories);
    }
    return true;
}

bool RunAssetImport(AssetImportJob & job, Foundation::ITaskContext & context) {
    job.Context = &context;
    if (context.IsCancellationRequested()) return job.Fail("Import cancelled");
    if (!Guid(job.Request.GuidString).IsValid() || !AssetArtifactIO::RelativePath(job.Request.SourcePath)) return job.Fail("Invalid source identity");
    Json::Value meta;
    std::string metaError;
    const char * importerName = AssetMetaFormat::ImporterTypeName(job.Request.Importer);
    if (!AssetArtifactIO::Parse(job.Request.MetaJson, meta) || !AssetMetaFormat::Validate(meta, metaError)
        || meta["Guid"].asString() != job.Request.GuidString
        || meta["Path"].asString() != job.Request.SourcePath
        || !importerName || meta["ImporterType"].asString() != importerName)
        return job.Fail("Invalid Meta snapshot: " + metaError);
    job.Settings = meta["ImportSettings"];
    job.Identities = meta["SubAssets"];
    for (const Json::Value & entry : job.Identities) {
        if (!entry["LocalID"].isUInt64() || entry["LocalID"].asUInt64() == 0 || !entry["Type"].isUInt()
            || !entry["SourceKey"].isString() || !job.UsedIDs.insert(entry["LocalID"].asUInt64()).second
            || !job.IDs.emplace(std::to_string(entry["Type"].asUInt()) + ":" + entry["SourceKey"].asString(), entry["LocalID"].asUInt64()).second)
            return job.Fail("Duplicate or invalid subasset identity");
    }
    const QDir root(QString::fromStdString(job.Request.AssetsRoot));
    const QFileInfo source(root.filePath(QString::fromStdString(job.Request.SourcePath)));
    if (!source.isFile() || !source.canonicalFilePath().startsWith(root.canonicalPath() + "/")) return job.Fail("Source is missing or outside Assets");
    job.SourceSize = source.size();
    job.SourceModified = source.lastModified();
    auto & manifest = job.Result.Manifest;
    const bool mayReuse = !job.Request.Force && job.Request.HasPrevious;
    if (!job.ReadFile(source.absoluteFilePath().toStdString(), mayReuse ? nullptr : &job.SourceBytes, manifest.SourceHash)) return false;
    const auto sourceVersions = job.ReadVersions;
    manifest.GuidString = job.Request.GuidString;
    manifest.SourcePath = job.Request.SourcePath;
    manifest.ImporterVersion = AssetImportAlgorithmVersion(job.Request.Importer);
    manifest.SettingsHash = AssetArtifactIO::Hash(QByteArray::fromStdString(
        std::to_string(static_cast<uint16_t>(job.Request.Importer)) + ":" + AssetArtifactIO::JsonText(job.Settings)));
    if(job.Request.Importer==eAssetImporterType::Shader || job.Request.Importer==eAssetImporterType::ComputeShader) {
        if(!job.Request.Compiler) return job.Fail("ShaderCompiler module is unavailable");
        manifest.ToolchainHash=job.Request.Compiler->GetToolchainHash();
    }
    bool cached = !job.Request.Force && job.Request.HasPrevious
        && job.Request.Previous.Directory == "Artifact" && !job.Request.Previous.Generation.empty() && job.Request.Previous.SourcePath == manifest.SourcePath
        && job.Request.Previous.SourceHash == manifest.SourceHash && job.Request.Previous.SettingsHash == manifest.SettingsHash
        && job.Request.Previous.ToolchainHash == manifest.ToolchainHash
        && job.Request.Previous.ImporterVersion == manifest.ImporterVersion && job.Request.Previous.Target == manifest.Target;
    if(cached&&!AnimationCachedInputsCurrent(job))cached=false;
    if(cached&&!ModelTextureSearchesCurrent(job.Request,job.Request.Previous,&job.SearchDirectoryVersions,&job.ValidationAvailable))cached=false;
    if (cached) {
        // A manually changed identity registry must never be overwritten by a cache hit.
        for (const auto & asset : job.Request.Previous.Assets) {
            if (asset.DataVersion != 2) { cached = false; break; }
            const std::string key = std::to_string(static_cast<uint16_t>(asset.Type)) + ":" + asset.SourceKey;
            const auto identity = job.IDs.find(key);
            if (identity == job.IDs.end() || identity->second != asset.LocalID) { cached = false; break; }
        }
    }
    if (cached) {
        for (const auto & dependency : job.Request.Previous.Dependencies) {
            if (!job.AddDependency(dependency.Path) || job.Result.Manifest.Dependencies.back().Hash != dependency.Hash) { cached = false; break; }
        }
        for (const auto & asset : job.Request.Previous.Assets) {
            std::string hash;
            const std::string path = job.Request.LibraryRoot + "/" + job.Request.Previous.Directory + "/" + asset.Path;
            if (!job.ReadFile(path, nullptr, hash) || job.ReadVersions.at(path).Size != asset.DataSize
                || hash != asset.Hash) { cached = false; break; }
        }
    }
    if (job.Result.Superseded) return false;
    job.Result.Error.clear();
    manifest.Dependencies.clear();
    if (!cached) { job.ReadVersions = sourceVersions; job.SearchDirectoryVersions.clear(); }
    if (cached) { job.Result.Manifest = job.Request.Previous; job.Result.Diagnostics=job.Request.Previous.Diagnostics; job.Result.Success = true; job.Result.Cached = true; return true; }
    if (mayReuse) {
        std::string sourceHash;
        if (!job.ReadFile(source.absoluteFilePath().toStdString(), &job.SourceBytes, sourceHash)) return false;
        if (sourceHash != manifest.SourceHash) {
            job.Result.Superseded = true; job.Result.Retry = true;
            return job.Fail("Source changed before decoding");
        }
    }
    const QString stage = QDir(QString::fromStdString(job.Request.LibraryRoot)).filePath("Staging/" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkpath(stage)) return job.Fail("Cannot create import stage");
    job.Stage = stage.toStdString();
    bool success = false;
    switch (job.Request.Importer) {
    case eAssetImporterType::Texture: success = ImportTexture(job); break;
    case eAssetImporterType::Model: success = ImportModel(job); break;
    case eAssetImporterType::Audio:
    case eAssetImporterType::Video: success = ImportMedia(job); break;
    case eAssetImporterType::Font: success = ImportFont(job); break;
    case eAssetImporterType::Folder: return job.Fail("Folders do not produce runtime assets");
    case eAssetImporterType::Prefab: return job.Fail("Prefab importing is not enabled");
    default: success = ImportDocument(job); break;
    }
    if (!success || context.IsCancellationRequested()) return job.Fail(job.Result.Error.empty() ? "Import cancelled or failed" : job.Result.Error);
    QFileInfo current(source.absoluteFilePath());
    if (!current.isFile() || current.size() != job.SourceSize || current.lastModified() != job.SourceModified) {job.Result.Superseded=true;job.Result.Retry=true;return job.Fail("Source changed during import");}
    // Re-read dependencies before publishing; a newly discovered dependency may not
    // yet be present in the main-thread reverse index while this first import runs.
    const auto dependencies = manifest.Dependencies;
    for (const auto & dependency : dependencies) if (!job.AddDependency(dependency.Path)) return false;
    if(!ModelTextureSearchesCurrent(job.Request,manifest,&job.SearchDirectoryVersions,&job.ValidationAvailable)) {
        job.Result.Superseded=true;job.Result.Retry=true;return job.Fail("Texture lookup changed during import");
    }
    manifest.SubAssetsJson = AssetArtifactIO::JsonText(job.Identities);
    manifest.Diagnostics=job.Result.Diagnostics;
    job.Result.Success = true;
    return true;
}
