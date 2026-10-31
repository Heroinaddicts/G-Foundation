#include "AssetsPipeline.h"
#include "AssetImportJob.h"
#include "IAssetsManager.h"
#include "IShaderCompiler.h"
#include "../Shared/AssetMetaFormat.h"

namespace {
uint32_t ValidationVersion(eAssetImporterType type) {
    return type==eAssetImporterType::AnimatorController||type==eAssetImporterType::AvatarMask||type==eAssetImporterType::AnimationClip?2:1;
}
Json::Value StampJson(const AssetFileStamp & stamp) {
    Json::Value value;
    value["Exists"] = stamp.Exists;
    value["Directory"] = stamp.Directory;
    value["Size"] = Json::UInt64(stamp.Size);
    value["ModifiedNanoseconds"] = Json::Int64(stamp.ModifiedNanoseconds);
    value["ChangedNanoseconds"] = Json::Int64(stamp.ChangedNanoseconds);
    value["Device"] = Json::UInt64(stamp.Device);
    value["FileID"] = Json::UInt64(stamp.FileID);
    return value;
}

bool ReadStampJson(const Json::Value & value, AssetFileStamp & stamp) {
    if (!value.isObject() || !value["Exists"].isBool() || !value["Directory"].isBool()
        || !value["Size"].isUInt64() || !value["ModifiedNanoseconds"].isInt64()
        || !value["ChangedNanoseconds"].isInt64() || !value["Device"].isUInt64() || !value["FileID"].isUInt64()) return false;
    stamp = {value["Exists"].asBool(), value["Directory"].asBool(), value["Size"].asUInt64(),
        value["ModifiedNanoseconds"].asInt64(), value["ChangedNanoseconds"].asInt64(),
        value["Device"].asUInt64(), value["FileID"].asUInt64()};
    return true;
}

std::string ManifestHash(const AssetImportManifest & manifest) {
    return AssetArtifactIO::Hash(QByteArray::fromStdString(AssetArtifactIO::JsonText(AssetArtifactIO::ToJson(manifest))));
}

bool MetaIdentity(IAssetMeta * meta, const AssetImportManifest & manifest, std::string & hash) {
    std::string text;
    std::string error;
    Json::Value value;
    if (!meta || !meta->SerializeMeta(text) || !AssetArtifactIO::Parse(text, value)
        || !AssetMetaFormat::Validate(value, error)
        || !AssetMetaFormat::ImporterTypeName(meta->_ImporterType)
        || value["Guid"].asString() != meta->_Guid.GetGuidString() || value["Path"].asString() != meta->GetPath()
        || value["ImporterType"].asString() != AssetMetaFormat::ImporterTypeName(meta->_ImporterType)
        || manifest.GuidString != meta->_Guid.GetGuidString() || manifest.SourcePath != meta->GetPath()
        || manifest.SettingsHash != AssetArtifactIO::Hash(QByteArray::fromStdString(
            std::to_string(static_cast<uint16_t>(meta->_ImporterType)) + ":" + AssetArtifactIO::JsonText(value["ImportSettings"])))
        || manifest.SubAssetsJson != AssetArtifactIO::JsonText(value["SubAssets"])) return false;
    hash = AssetArtifactIO::Hash(QByteArray::fromStdString(AssetArtifactIO::JsonText(value)));
    return true;
}

bool Observe(const Json::Value & entry, const QDir & assets, const QDir & library,
    bool directory, std::string & key) {
    if (!entry.isObject() || !entry["Root"].isUInt() || entry["Root"].asUInt() > 1 || !entry["Path"].isString()) return false;
    const std::string relative = entry["Path"].asString();
    if ((!directory || !relative.empty()) && !AssetArtifactIO::RelativePath(relative)) return false;
    const QDir & root = entry["Root"].asUInt() == 0 ? assets : library;
    const QString absolute = root.filePath(QString::fromStdString(relative));
    AssetFileStamp previous, current;
    if (!ReadStampJson(entry["Stamp"], previous) || !ReadAssetFileStamp(absolute.toStdString(), current)
        || previous != current || (current.Exists && current.Directory != directory)) return false;
    if (current.Exists) {
        const QString canonical = QFileInfo(absolute).canonicalFilePath();
        const QString canonicalRoot = root.canonicalPath();
        if (canonicalRoot.isEmpty() || (canonical != canonicalRoot && !canonical.startsWith(canonicalRoot + "/"))) return false;
    }
    key = std::to_string(entry["Root"].asUInt()) + ":" + relative;
    return true;
}

bool AppendObservation(Json::Value & entries, const std::string & path, const AssetFileStamp & version,
    const QDir & assets, const QDir & library) {
    AssetFileStamp current;
    if (!ReadAssetFileStamp(path, current) || current != version) return false;
    Json::Value entry;
    const QString absolute = QDir::cleanPath(QString::fromStdString(path));
    const QString assetsPath = assets.absolutePath(), libraryPath = library.absolutePath();
    QString relative;
    if (absolute == assetsPath || absolute.startsWith(assetsPath + "/")) {
        entry["Root"] = 0U; relative = assets.relativeFilePath(absolute);
    } else if (absolute == libraryPath || absolute.startsWith(libraryPath + "/")) {
        entry["Root"] = 1U; relative = library.relativeFilePath(absolute);
    } else return false;
    if (relative == ".") relative.clear();
    if (!relative.isEmpty() && !AssetArtifactIO::RelativePath(relative.toStdString())) return false;
    entry["Path"] = relative.toStdString();
    entry["Stamp"] = StampJson(current);
    entries.append(std::move(entry));
    return true;
}
}

bool AssetsPipeline::CanReuseAsset(IAssetMeta * meta) const {
    if (!meta || !_Library || !_Project || meta->_ImporterType == eAssetImporterType::Folder
        || meta->_ImporterType == eAssetImporterType::Script) return false;
    AssetImportManifest manifest;
    std::string text, metaHash;
    Json::Value validation;
    if (!_Library->GetManifest(meta->_Guid, manifest) || manifest.Generation.empty()
        || manifest.Directory != "Artifact" || manifest.Target != "Portable"
        || manifest.ImporterVersion != AssetImportAlgorithmVersion(meta->_ImporterType)
        || !MetaIdentity(meta, manifest, metaHash)
        || !_Library->GetImportValidation(meta->_Guid, text) || !AssetArtifactIO::Parse(text, validation) || !validation.isObject()
        || (!validation["Version"].isUInt()||validation["Version"].asUInt()!=ValidationVersion(meta->_ImporterType)) || validation["Generation"] != manifest.Generation
        || validation["ManifestHash"] != ManifestHash(manifest) || validation["MetaHash"] != metaHash
        || !validation["Files"].isArray() || !validation["Directories"].isArray()) return false;
    const bool shader = meta->_ImporterType == eAssetImporterType::Shader || meta->_ImporterType == eAssetImporterType::ComputeShader;
    if (manifest.ToolchainHash != (shader && _ShaderCompiler ? _ShaderCompiler->GetToolchainHash() : "")) return false;
    const QDir assets(QDir(QString::fromStdString(_Project->GetProjectRoot())).filePath("Assets"));
    const QDir library(QString::fromStdString(_Library->GetLibraryRoot()));
    std::set<std::string> required{"0:" + manifest.SourcePath};
    for (const auto & dependency : manifest.Dependencies) {
        required.insert("0:" + dependency.Path);
        required.insert("0:" + dependency.Path + ".meta");
    }
    for (const auto & artifact : manifest.Assets) {
        if (artifact.DataVersion != 2) return false;
        required.insert("1:" + manifest.Directory + "/" + artifact.Path);
    }
    if(ValidationVersion(meta->_ImporterType)==2) {
        if(!validation["PreparedInputs"].isArray())return false;
        std::set<std::string> references;
        for(const auto& input:validation["PreparedInputs"]) {
            if(!input.isObject()||!input["Guid"].isString()||!Guid(input["Guid"].asString()).IsValid()
                ||!input["LocalID"].isUInt64()||!input["LocalID"].asUInt64()||!input["Path"].isString()
                ||!input["Hash"].isString()||!input["DescriptorHash"].isString())return false;
            const auto guid=input["Guid"].asString();const auto localID=input["LocalID"].asUInt64();
            if(guid==manifest.GuidString||!references.insert(guid+"_"+std::to_string(localID)).second)return false;
            AssetImportManifest producer;
            if(!_Library->GetManifest(Guid(guid),producer)||producer.Directory!="Artifact")return false;
            bool sourceDependency=false;
            for(const auto& dependency:manifest.Dependencies)if(dependency.Path==producer.SourcePath)sourceDependency=true;
            if(!sourceDependency)return false;
            bool matched=false;
            for(const auto& artifact:producer.Assets)if(artifact.LocalID==localID) {
                const auto relative=producer.Directory+"/"+artifact.Path;
                if(artifact.DataVersion!=2||!AssetArtifactIO::RelativePath(relative)||input["Path"]!=relative
                    ||input["Hash"]!=artifact.Hash||input["DescriptorHash"]!=AssetArtifactIO::Hash(QByteArray::fromStdString(artifact.DescriptorJson)))return false;
                required.insert("1:"+relative);matched=true;break;
            }
            if(!matched)return false;
        }
    }
    std::set<std::string> found;
    std::map<std::string, AssetFileStamp> versions;
    for (const auto & entry : validation["Files"]) {
        std::string key;
        if (!Observe(entry, assets, library, false, key) || !found.insert(key).second) return false;
        AssetFileStamp version;
        if (!ReadStampJson(entry["Stamp"], version)) return false;
        versions.emplace(key, version);
    }
    if (found != required) return false;
    if (!manifest.TextureSearches.empty() && validation["Directories"].empty()) return false;
    for (const auto & entry : validation["Directories"]) {
        std::string key;
        if (!Observe(entry, assets, library, true, key)) return false;
    }
    // 复用数据库已确认的内容指纹；后续多个依赖者无需重新哈希未变化的源文件。
    if (!_Fingerprints.Seed(assets.filePath(QString::fromStdString(manifest.SourcePath)).toStdString(),
        {versions.at("0:" + manifest.SourcePath), manifest.SourceHash})) return false;
    for (const auto & artifact : manifest.Assets) {
        const std::string relative = manifest.Directory + "/" + artifact.Path;
        if (!_Fingerprints.Seed(library.filePath(QString::fromStdString(relative)).toStdString(),
            {versions.at("1:" + relative), artifact.Hash})) return false;
    }
    return true;
}

void AssetsPipeline::RestoreFileFingerprints(const AssetImportManifest & manifest, const std::string & assetsRoot) {
    const auto* owner=_Assets->QueryAsset(Guid(manifest.GuidString));
    if(!owner)return;
    std::string text;
    Json::Value validation;
    if (!_Library->GetImportValidation(Guid(manifest.GuidString), text) || !AssetArtifactIO::Parse(text, validation)
        || !validation.isObject() || (!validation["Version"].isUInt()||validation["Version"].asUInt()!=ValidationVersion(owner->_ImporterType)) || validation["Generation"] != manifest.Generation
        || validation["ManifestHash"] != ManifestHash(manifest) || !validation["Files"].isArray()) return;
    const QDir assets(QString::fromStdString(assetsRoot)), library(QString::fromStdString(_Library->GetLibraryRoot()));
    std::map<std::string, std::string> hashes{{"0:" + manifest.SourcePath, manifest.SourceHash}};
    for (const auto & artifact : manifest.Assets)
        hashes.emplace("1:" + manifest.Directory + "/" + artifact.Path, artifact.Hash);
    for (const auto & entry : validation["Files"]) {
        std::string key;
        if (!Observe(entry, assets, library, false, key)) continue;
        const auto hash = hashes.find(key);
        if (hash == hashes.end()) continue;
        AssetFileStamp version;
        if (!ReadStampJson(entry["Stamp"], version)) continue;
        const QDir & root = entry["Root"].asUInt() == 0 ? assets : library;
        _Fingerprints.Seed(root.filePath(QString::fromStdString(entry["Path"].asString())).toStdString(), {version, hash->second});
    }
}

void AssetsPipeline::SaveImportValidation(const AssetImportJob & job, const AssetImportResult & result) {
    if (!job.ValidationAvailable) return;
    IAssetMeta * meta = _Assets->QueryAsset(Guid(job.Request.GuidString));
    std::string metaHash;
    if (!MetaIdentity(meta, result.Manifest, metaHash)) return;
    const QDir assets(QString::fromStdString(job.Request.AssetsRoot));
    const QDir library(QString::fromStdString(job.Request.LibraryRoot));
    Json::Value validation;
    validation["Version"] = ValidationVersion(meta->_ImporterType);
    validation["Generation"] = result.Manifest.Generation;
    validation["ManifestHash"] = ManifestHash(result.Manifest);
    validation["MetaHash"] = metaHash;
    validation["Files"] = Json::Value(Json::arrayValue);
    validation["Directories"] = Json::Value(Json::arrayValue);
    validation["PreparedInputs"] = Json::Value(Json::arrayValue);
    for(const auto& prepared:job.Request.PreparedResources) {
        const auto& resource=prepared.second;
        if(!job.ReadVersions.count(resource.Path))continue;
        const auto separator=prepared.first.rfind('_');if(separator==std::string::npos)return;
        const auto guid=prepared.first.substr(0,separator);
        const QString relative=library.relativeFilePath(QString::fromStdString(resource.Path));
        if(!AssetArtifactIO::RelativePath(relative.toStdString())||!relative.startsWith("Artifact/"))return;
        Json::Value input;input["Guid"]=guid;input["LocalID"]=Json::UInt64(std::stoull(prepared.first.substr(separator+1)));
        input["Path"]=relative.toStdString();input["Hash"]=resource.Hash;
        input["DescriptorHash"]=AssetArtifactIO::Hash(QByteArray::fromStdString(resource.DescriptorJson));
        validation["PreparedInputs"].append(input);
    }
    auto versions = job.ReadVersions;
    for (const auto & artifact : result.Manifest.Assets) {
        const std::string path = library.filePath(QString::fromStdString(result.Manifest.Directory + "/" + artifact.Path)).toStdString();
        if (!versions.count(path)) {
            AssetFileStamp stamp;
            if (!ReadAssetFileStamp(path, stamp) || !stamp.Exists || stamp.Directory || stamp.Size != artifact.DataSize) return;
            versions.emplace(path, stamp);
        }
    }
    for (const auto & version : versions)
        if (!AppendObservation(validation["Files"], version.first, version.second, assets, library)) return;
    auto directories = job.SearchDirectoryVersions;
    bool directoriesChanged = false;
    for (const auto & version : directories) {
        AssetFileStamp current;
        if (!ReadAssetFileStamp(version.first, current) || current != version.second) {
            directoriesChanged = true; break;
        }
    }
    if (directoriesChanged) {
        // 保存自己的 .meta 也会改变父目录版本；重新确认查找结果后才能建立最终基线。
        bool available = true;
        if (!ModelTextureSearchesCurrent(job.Request, result.Manifest, &directories, &available) || !available) return;
    }
    for (const auto & version : directories)
        if (!AppendObservation(validation["Directories"], version.first, version.second, assets, library)) return;
    _Library->SaveImportValidation(meta->_Guid, result.Manifest.Generation, AssetArtifactIO::JsonText(validation));
}
