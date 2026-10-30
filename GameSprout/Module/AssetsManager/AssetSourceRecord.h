#pragma once

#include "IAssetsManager.h"
#include "../Shared/AssetArtifactIO.h"

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
            || !value["MetaJson"].isString() || !value["Status"].isString() || !value["Error"].isString()) return false;
        const std::string status = value["Status"].asString();
        if (status != "Pending" && status != "Ready" && status != "Failed" && status != "NotRequired") return false;
        record.GuidString = value["Guid"].asString();
        record.Path = value["Path"].asString();
        record.Importer = static_cast<eAssetImporterType>(value["ImporterType"].asUInt());
        record.SourceHash = value["SourceHash"].asString();
        record.SettingsHash = value["SettingsHash"].asString();
        for (const char * key : {"SourceSize", "SourceModifyTime", "MetaModifyTime"})
            if (value.isMember(key) && !value[key].isInt64()) return false;
        record.SourceSize = value.get("SourceSize", Json::Int64(-1)).asInt64();
        record.SourceModifyTime = value.get("SourceModifyTime", Json::Int64(-1)).asInt64();
        record.MetaModifyTime = value.get("MetaModifyTime", Json::Int64(-1)).asInt64();
        record.MetaJson = value["MetaJson"].asString();
        record.Status = status;
        record.Error = value["Error"].asString();
        return true;
    }
};
