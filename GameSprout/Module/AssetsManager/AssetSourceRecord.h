#pragma once

#include "IAssetsManager.h"
#include "../Shared/AssetArtifactIO.h"
#include "../Shared/AssetMetaFormat.h"

// Current source observations belong to AssetsManager. Successful generations stay
// in LibraryManager's separate Manifests table, including after an import failure.
struct AssetSourceRecord {
    std::string GuidString;
    std::string Path;
    eAssetImporterType Importer = eAssetImporterType::Unknown;
    std::string SourceHash; // Empty until a worker has read the current source.
    std::string SettingsHash;
    int64_t SourceSize = -1;
    int64_t SourceModifyTime = -1; // UTC milliseconds; observations, not successful-import markers.
    int64_t MetaModifyTime = -1;
    std::string MetaJson; // Last valid Meta, for recovering a missing sidecar.
    std::string Status = "Pending"; // Pending / Ready / Failed / NotRequired
    std::string Error;

    Json::Value ToJson() const {
        Json::Value value;
        value["Version"] = 1;
        value["Guid"] = GuidString;
        value["Path"] = Path;
        value["ImporterType"] = static_cast<uint16_t>(Importer);
        value["SourceHash"] = SourceHash;
        value["SettingsHash"] = SettingsHash;
        value["SourceSize"] = Json::Int64(SourceSize);
        value["SourceModifyTime"] = Json::Int64(SourceModifyTime);
        value["MetaModifyTime"] = Json::Int64(MetaModifyTime);
        value["MetaJson"] = MetaJson;
        value["Status"] = Status;
        value["Error"] = Error;
        return value;
    }

    static bool FromJson(const Json::Value & value, AssetSourceRecord & record) {
        if (!value.isObject() || value["Version"] != 1 || !value["Guid"].isString()
            || !Guid(value["Guid"].asString()).IsValid() || !value["Path"].isString()
            || !AssetArtifactIO::RelativePath(value["Path"].asString())
            || !value["ImporterType"].isUInt() || value["ImporterType"].asUInt() > UINT16_MAX
            || !value["SourceHash"].isString() || !value["SettingsHash"].isString()
            || !value["SourceSize"].isInt64() || !value["SourceModifyTime"].isInt64() || !value["MetaModifyTime"].isInt64()
            || !value["MetaJson"].isString() || !value["Status"].isString() || !value["Error"].isString()) return false;
        const std::string status = value["Status"].asString();
        if (status != "Pending" && status != "Ready" && status != "Failed" && status != "NotRequired") return false;
        const std::string metaText = value["MetaJson"].asString();
        if (!metaText.empty()) {
            Json::Value meta;
            std::string error;
            const char* importer = AssetMetaFormat::ImporterTypeName(static_cast<eAssetImporterType>(value["ImporterType"].asUInt()));
            if (!AssetArtifactIO::Parse(metaText, meta) || !AssetMetaFormat::Validate(meta, error)
                || !importer
                || meta["Guid"] != value["Guid"]
                || meta["ImporterType"].asString() != importer) return false;
        }
        record.GuidString = value["Guid"].asString();
        record.Path = value["Path"].asString();
        record.Importer = static_cast<eAssetImporterType>(value["ImporterType"].asUInt());
        record.SourceHash = value["SourceHash"].asString();
        record.SettingsHash = value["SettingsHash"].asString();
        record.SourceSize = value["SourceSize"].asInt64();
        record.SourceModifyTime = value["SourceModifyTime"].asInt64();
        record.MetaModifyTime = value["MetaModifyTime"].asInt64();
        record.MetaJson = value["MetaJson"].asString();
        record.Status = status;
        record.Error = value["Error"].asString();
        return true;
    }
};
