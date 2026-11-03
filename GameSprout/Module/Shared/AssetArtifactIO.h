#pragma once

#include "IAssetsPipeline.h"
#include <JsonCpp/json.hpp>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QCryptographicHash>
#include <sstream>
#include <set>

namespace AssetArtifactIO {
// Coordinate-bearing resources use the current Unity world-space contract.
// This is a type-specific current format, not a fallback for old RH payloads.
inline constexpr uint32_t CurrentDataVersion(eAssetType type) {
    switch(type) {
    case eAssetType::Mesh:
    case eAssetType::Model:
    case eAssetType::Skeleton:
    case eAssetType::AnimationClip:
    case eAssetType::Avatar:
        return 3;
    default:
        return 2;
    }
}
inline std::string JsonText(const Json::Value & value) {
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    writer["emitUTF8"] = true;
    return Json::writeString(writer, value);
}
inline bool Parse(const std::string & text, Json::Value & value) {
    Json::CharReaderBuilder reader;
    reader["rejectDupKeys"] = true;
    reader["failIfExtra"] = true;
    std::istringstream input(text);
    std::string error;
    return Json::parseFromStream(reader, input, &value, &error);
}
inline bool RelativePath(const std::string & path) {
    const QString value = QDir::fromNativeSeparators(QString::fromStdString(path));
    return !value.isEmpty() && !value.contains(QChar('\0')) && QDir::isRelativePath(value)
        && QDir::cleanPath(value) == value && value != "." && value != ".." && !value.startsWith("../");
}
inline std::string Hash(const QByteArray & bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}
inline bool Write(const QString & path, const QByteArray & bytes, std::string & error) {
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = "Cannot write " + path.toStdString() + ": " + file.errorString().toStdString();
        return false;
    }
    return true;
}
inline Json::Value ToJson(const AssetImportManifest & manifest) {
    Json::Value value(Json::objectValue);
    value["Version"] = 3;
    value["Guid"] = manifest.GuidString;
    value["SourcePath"] = manifest.SourcePath;
    value["SourceHash"] = manifest.SourceHash;
    value["SettingsHash"] = manifest.SettingsHash;
    value["ToolchainHash"] = manifest.ToolchainHash;
    value["Target"] = manifest.Target;
    value["ImporterVersion"] = manifest.ImporterVersion;
    value["Directory"] = manifest.Directory;
    value["Generation"] = manifest.Generation;
    Json::Value identities;
    if (!Parse(manifest.SubAssetsJson, identities)) identities = Json::Value(Json::arrayValue);
    value["SubAssets"] = identities;
    value["Assets"] = Json::Value(Json::arrayValue);
    for (const AssetArtifact & asset : manifest.Assets) {
        Json::Value item;
        item["LocalID"] = static_cast<Json::UInt64>(asset.LocalID);
        item["Type"] = static_cast<uint16_t>(asset.Type);
        item["Name"] = asset.Name;
        item["SourceKey"] = asset.SourceKey;
        item["Path"] = asset.Path;
        item["Hash"] = asset.Hash;
        item["DataVersion"] = asset.DataVersion;
        item["DataSize"] = Json::UInt64(asset.DataSize);
        Json::Value descriptor;
        if (!Parse(asset.DescriptorJson, descriptor)) descriptor = Json::Value(Json::objectValue);
        item["Descriptor"] = descriptor;
        value["Assets"].append(item);
    }
    value["Dependencies"] = Json::Value(Json::arrayValue);
    for (const AssetDependency & dependency : manifest.Dependencies) {
        Json::Value item;
        item["Path"] = dependency.Path;
        item["Hash"] = dependency.Hash;
        value["Dependencies"].append(item);
    }
    value["ReferencedGuids"]=Json::Value(Json::arrayValue);
    for(const auto& guid:manifest.ReferencedGuids)value["ReferencedGuids"].append(guid);
    value["GuidSourcePaths"]=Json::Value(Json::objectValue);
    for(const auto& [guid,path]:manifest.GuidSourcePaths)value["GuidSourcePaths"][guid]=path;
    value["TextureSearches"] = Json::Value(Json::arrayValue);
    for(const auto& search:manifest.TextureSearches) {
        Json::Value item;
        item["Filename"]=search.Filename;item["RelativeFilename"]=search.RelativeFilename;
        item["ResolvedPath"]=search.ResolvedPath;item["Reason"]=search.Reason;value["TextureSearches"].append(std::move(item));
    }
    value["Diagnostics"]=manifest.Diagnostics;
    return value;
}
inline bool FromJson(const Json::Value & value, AssetImportManifest & manifest) {
    if (!value.isObject() || value["Version"] != 3
        || !value["Guid"].isString() || !Guid(value["Guid"].asString()).IsValid()
        || !value["SourcePath"].isString() || !RelativePath(value["SourcePath"].asString())
        || value["Directory"] != "Artifact"
        || !value["SourceHash"].isString() || !value["SettingsHash"].isString() || !value["ToolchainHash"].isString()
        || !value["Target"].isString() || !value["ImporterVersion"].isUInt()
        || !value["Generation"].isString() || value["Generation"].asString().empty()
        || !value["TextureSearches"].isArray() || !value["Diagnostics"].isString()
        || !value["Assets"].isArray() || !value["Dependencies"].isArray()
        || !value["SubAssets"].isArray() || !value["ReferencedGuids"].isArray()
        || !value["GuidSourcePaths"].isObject()) return false;
    AssetImportManifest result;
    result.GuidString = value["Guid"].asString();
    result.SourcePath = value["SourcePath"].asString();
    result.SourceHash = value["SourceHash"].asString();
    result.SettingsHash = value["SettingsHash"].asString();
    result.ToolchainHash = value["ToolchainHash"].asString();
    result.Target = value["Target"].asString();
    result.ImporterVersion = value["ImporterVersion"].asUInt();
    result.Directory = value["Directory"].asString();
    result.Generation = value["Generation"].asString();
    result.SubAssetsJson = JsonText(value["SubAssets"]);
    std::set<uint64_t> ids;
    for (const Json::Value & item : value["Assets"]) {
        if (!item.isObject() || !item["LocalID"].isUInt64() || item["LocalID"].asUInt64() == 0
            || !ids.insert(item["LocalID"].asUInt64()).second || !item["Type"].isUInt()
            || item["Type"].asUInt() > 65535 || !item["Name"].isString() || !item["SourceKey"].isString()
            || !item["Path"].isString()
            || item["Path"].asString() != result.GuidString + "_" + std::to_string(item["LocalID"].asUInt64()) + ".gsasset"
            || !item["Hash"].isString()) return false;
        AssetArtifact asset{item["LocalID"].asUInt64(), static_cast<eAssetType>(item["Type"].asUInt()),
            item["Name"].asString(), item["SourceKey"].asString(), item["Path"].asString(), item["Hash"].asString()};
        if (!item["DataVersion"].isUInt() || item["DataVersion"].asUInt() != CurrentDataVersion(asset.Type)
            || !item["DataSize"].isUInt64() || !item["Descriptor"].isObject()
            || (item["Descriptor"]["Encoding"] != "RawBytes" && item["Descriptor"]["Encoding"] != "BinaryValueLE1")) return false;
        asset.DataVersion = item["DataVersion"].asUInt();
        asset.DataSize = item["DataSize"].asUInt64();
        asset.DescriptorJson = JsonText(item["Descriptor"]);
        result.Assets.push_back(std::move(asset));
    }
    for (const Json::Value & item : value["Dependencies"]) {
        if (!item["Path"].isString() || !RelativePath(item["Path"].asString()) || !item["Hash"].isString()) return false;
        result.Dependencies.push_back({item["Path"].asString(), item["Hash"].asString()});
    }
    std::set<std::string> referenced;
    for(const auto& item:value["ReferencedGuids"]) {
        if(!item.isString()||!Guid(item.asString()).IsValid()||!referenced.insert(item.asString()).second)return false;
        result.ReferencedGuids.push_back(item.asString());
    }
    if(value["GuidSourcePaths"].size()!=referenced.size())return false;
    for(const auto& guid:value["GuidSourcePaths"].getMemberNames()) {
        const auto& path=value["GuidSourcePaths"][guid];
        if(!referenced.count(guid)||!path.isString()||(!path.asString().empty()&&!RelativePath(path.asString())))return false;
        result.GuidSourcePaths.emplace(guid,path.asString());
    }
    const Json::Value& searches=value["TextureSearches"];
    for(const auto& item:searches) {
        if(!item.isObject()||!item["Filename"].isString()||!item["RelativeFilename"].isString()||!item["ResolvedPath"].isString()||!item["Reason"].isString()
            ||(!item["ResolvedPath"].asString().empty()&&!RelativePath(item["ResolvedPath"].asString())))return false;
        result.TextureSearches.push_back({item["Filename"].asString(),item["RelativeFilename"].asString(),item["ResolvedPath"].asString(),item["Reason"].asString()});
    }
    result.Diagnostics=value["Diagnostics"].asString();
    manifest = std::move(result);
    return true;
}
}
