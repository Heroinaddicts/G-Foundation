#include "AssetImportJob.h"
#include "ModelTextureResolver.h"
#include "AnimationResourceValidation.h"
#include <QUuid>
#include "../Shared/AssetValueBinary.h"
#include "../Shared/AssetMetaFormat.h"
#include "IShaderCompiler.h"
#include "RuntimeResourceApi.h"

namespace {
bool AppendMeshCollision(AssetImportJob & job, Json::Value & descriptor, QByteArray & bytes) {
    const auto cook = job.Request.MeshCollision.Cook;
    if (!cook) return true;
    if (job.Cancelled()) return false;

    const Json::Value & layout = descriptor;
    if (layout["IndexFormat"] != "UInt32" || !layout["VertexCount"].isUInt() || !layout["VertexStride"].isUInt()
        || !layout["IndexCount"].isUInt() || !layout["IndexOffset"].isUInt64()
        || !layout["VertexAttributes"].isArray()) return true;
    for (const auto & attribute : layout["VertexAttributes"]) {
        if (attribute["Semantic"] != "Position" || attribute["Format"] != "Float32x3"
            || !attribute["Offset"].isUInt()) continue;
        GameSproutRuntime::MeshCollisionBakeInput input;
        input.Bytes = {reinterpret_cast<const std::uint8_t *>(bytes.constData()), static_cast<std::size_t>(bytes.size())};
        input.VertexCount = layout["VertexCount"].asUInt();
        input.VertexStride = layout["VertexStride"].asUInt();
        input.IndexCount = layout["IndexCount"].asUInt();
        input.PositionOffset = attribute["Offset"].asUInt();
        input.IndexOffset = layout["IndexOffset"].asUInt64();
        GameSproutRuntime::CookedMeshCollisionData output;
        std::string error;
        const bool cooked = cook(input, output, error);
        if (job.Cancelled()) return false;
        if (!cooked) return true;

        // Preserve valid render geometry when Jolt cannot represent a shape or
        // an appended collision payload would exceed the runtime read limit.
        constexpr std::uint64_t maximumArtifactBytes = 512ull * 1024 * 1024;
        const auto append = [&](const char * kind, const std::vector<std::uint8_t> & payload) {
            const auto offset = static_cast<std::uint64_t>(bytes.size());
            if (payload.empty() || offset >= maximumArtifactBytes || payload.size() > maximumArtifactBytes - offset) return;
            auto & range = descriptor["Collision"][kind];
            range["Offset"] = Json::UInt64(offset);
            range["Size"] = Json::UInt64(payload.size());
            bytes.append(reinterpret_cast<const char *>(payload.data()), static_cast<qsizetype>(payload.size()));
        };
        append("Triangle", output.Triangle);
        append("Convex", output.Convex);
        break;
    }
    return true;
}
}

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
    Json::Value metadata, QByteArray payload) {
    if (Context && Context->IsCancellationRequested()) return Fail("Import cancelled");
    const std::string typedKey = std::to_string(static_cast<uint16_t>(type)) + ":" + key;
    if (!ProducedKeys.insert(typedKey).second) return Fail("Duplicate imported object identity: " + key);
    const uint64_t id = ID(type, key, name);
    const bool structured = type == eAssetType::AnimatorController || type == eAssetType::Model || type == eAssetType::Skeleton
        || type == eAssetType::Avatar || type == eAssetType::AvatarMask || type == eAssetType::Material
        || metadata["Format"] == "GameSproutDocument" || metadata["Format"] == "GameSproutPrefab";
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
        bytes = std::move(payload);
        descriptor = std::move(metadata);
        descriptor["Encoding"] = "RawBytes";
        if (type == eAssetType::Mesh && !AppendMeshCollision(*this, descriptor, bytes)) return false;
    }
    // Resource type, identity, layout and byte size belong to ArtifactDB. The
    // file contains only raw payload or the structural binary body, no JSON header.
    const std::string filename = std::to_string(id) + ".gsasset";
    if (!AssetArtifactIO::Write(QDir(QString::fromStdString(Stage)).filePath(QString::fromStdString(filename)), bytes, Result.Error)) return false;
    AssetArtifact artifact{id, type, name, key, filename, AssetArtifactIO::Hash(bytes)};
    artifact.DataVersion = AssetArtifactIO::CurrentDataVersion(type);
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

bool AssetImportJob::AddDependency(const std::string & relativePath, QByteArray * output, bool recheck) {
    if (Cancelled()) return false;
    if (!AssetArtifactIO::RelativePath(relativePath)) return Fail("Invalid dependency path: " + relativePath);
    const QDir root(QString::fromStdString(Request.AssetsRoot));
    const QFileInfo info(root.filePath(QString::fromStdString(relativePath)));
    const auto known=DependencyIndices.find(relativePath);
    if (known!=DependencyIndices.end()) {
        // Keep repeated callers sensitive to concurrent replacement/deletion,
        // while reusing the Meta parse/hash already captured by this job.
        if (recheck) {
            const QString canonicalRoot=root.canonicalPath();
            if (canonicalRoot.isEmpty() || !info.canonicalFilePath().startsWith(canonicalRoot + "/")) {
                Result.Superseded=true;Result.Retry=true;
                return Fail("Dependency moved outside Assets during import: " + relativePath);
            }
        }
        const std::string sourcePath=info.absoluteFilePath().toStdString();
        for (const auto& observedPath : {sourcePath,sourcePath+".meta"}) {
            AssetFileStamp current;
            const auto observed=ReadVersions.find(observedPath);
            if (observed==ReadVersions.end() || !ReadAssetFileStamp(observedPath,current) || current!=observed->second) {
                Result.Superseded=true;Result.Retry=true;
                return Fail("Dependency changed during import: " + relativePath);
            }
        }
        if (output) {
            std::string hash;
            if (!ReadFile(sourcePath,output,hash)) return false;
        }
        return true;
    }
    const auto retryDisappearedReference=[&] {
        const QFileInfo current(info.absoluteFilePath());
        if(current.exists()||current.isSymLink())return;
        for(const auto& [guid,path]:Result.Manifest.GuidSourcePaths)if(path==relativePath) {
            Result.Superseded=true;Result.Retry=true;return;
        }
    };
    if (!info.isFile() || !info.canonicalFilePath().startsWith(root.canonicalPath() + "/")) {
        retryDisappearedReference();return Fail("Missing or external dependency: " + relativePath);
    }
    std::string sourceHash;
    if (!ReadFile(info.absoluteFilePath().toStdString(), output, sourceHash)) {retryDisappearedReference();return false;}
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
    DependencyIndices.emplace(relativePath,Result.Manifest.Dependencies.size());
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
    } else manifest.ToolchainHash=MeshCollisionToolchainHash(job.Request.Importer, job.Request.MeshCollision);
    bool cached = !job.Request.Force && job.Request.HasPrevious
        && job.Request.Previous.Directory == "Artifact" && !job.Request.Previous.Generation.empty() && job.Request.Previous.SourcePath == manifest.SourcePath
        && job.Request.Previous.SourceHash == manifest.SourceHash && job.Request.Previous.SettingsHash == manifest.SettingsHash
        && job.Request.Previous.ToolchainHash == manifest.ToolchainHash
        && job.Request.Previous.ImporterVersion == manifest.ImporterVersion && job.Request.Previous.Target == manifest.Target;
    if(cached&&!AssetGuidSourcesCurrent(job.Request,job.Request.Previous))cached=false;
    if(cached&&!AnimationCachedInputsCurrent(job))cached=false;
    if(cached&&!ModelTextureSearchesCurrent(job.Request,job.Request.Previous,&job.SearchDirectoryVersions,&job.ValidationAvailable))cached=false;
    if (cached) {
        // A manually changed identity registry must never be overwritten by a cache hit.
        for (const auto & asset : job.Request.Previous.Assets) {
            if (asset.DataVersion != AssetArtifactIO::CurrentDataVersion(asset.Type)) { cached = false; break; }
            const std::string key = std::to_string(static_cast<uint16_t>(asset.Type)) + ":" + asset.SourceKey;
            const auto identity = job.IDs.find(key);
            if (identity == job.IDs.end() || identity->second != asset.LocalID) { cached = false; break; }
        }
    }
    if (cached) {
        for (const auto & dependency : job.Request.Previous.Dependencies) {
            if (!job.AddDependency(dependency.Path) || job.Result.Manifest.Dependencies.at(job.DependencyIndices.at(dependency.Path)).Hash != dependency.Hash) { cached = false; break; }
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
    job.DependencyIndices.clear();
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
    default: success = ImportDocument(job); break;
    }
    if (!success || context.IsCancellationRequested()) return job.Fail(job.Result.Error.empty() ? "Import cancelled or failed" : job.Result.Error);
    QFileInfo current(source.absoluteFilePath());
    if (!current.isFile() || current.size() != job.SourceSize || current.lastModified() != job.SourceModified) {job.Result.Superseded=true;job.Result.Retry=true;return job.Fail("Source changed during import");}
    // Recheck exact dependency file versions before publishing; a newly discovered dependency may not
    // yet be present in the main-thread reverse index while this first import runs.
    for (const auto & dependency : manifest.Dependencies) if (!job.AddDependency(dependency.Path,nullptr,true)) return false;
    if(!ModelTextureSearchesCurrent(job.Request,manifest,&job.SearchDirectoryVersions,&job.ValidationAvailable)) {
        job.Result.Superseded=true;job.Result.Retry=true;return job.Fail("Texture lookup changed during import");
    }
    manifest.SubAssetsJson = AssetArtifactIO::JsonText(job.Identities);
    manifest.Diagnostics=job.Result.Diagnostics;
    job.Result.Success = true;
    return true;
}
