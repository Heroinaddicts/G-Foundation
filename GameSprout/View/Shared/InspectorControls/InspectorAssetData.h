#pragma once
#include "InspectorPanel.h"
#include "IAssetsManager.h"
#include "ILibraryManager.h"
#include "AssetDisplayNames.h"
#include <QFileInfo>
#include <sstream>

namespace InspectorAssetData {
inline std::string JsonText(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}
inline bool ReadMeta(IAssetMeta& meta, Json::Value& value, std::string& error) {
    std::string snapshot;
    if (!meta.SerializeMeta(snapshot)) { error = "无法读取资源设置。"; return false; }
    Json::CharReaderBuilder builder;
    std::istringstream input(snapshot);
    if (!Json::parseFromStream(builder, input, &value, &error)) return false;
    error.clear(); return true;
}
inline IAssetMeta* Meta(const InspectorServices& services, const InspectorTarget& target) {
    return services.Assets && target.Assets.size() == 1
        ? services.Assets->QueryAsset(Guid(target.Assets.front().GuidString)) : nullptr;
}
inline IAsset* Asset(const InspectorServices& services, const InspectorTarget& target) {
    return services.Library && target.Assets.size() == 1 && target.Assets.front().IsSubAsset
        ? services.Library->QueryAsset(Guid(target.Assets.front().GuidString), target.Assets.front().LocalID) : nullptr;
}
inline InspectorPresentation Describe(const InspectorServices& services, const InspectorTarget& target) {
    InspectorPresentation description;
    auto* meta = Meta(services, target);
    if (!meta) return description;
    const auto& selected = target.Assets.front();
    description.Path = "Assets/" + QString::fromStdString(meta->GetPath());
    description.Name = QFileInfo(QString::fromStdString(meta->GetPath())).fileName();
    description.Guid = QString::fromStdString(selected.GuidString);
    description.LocalID = selected.IsSubAsset ? QString::number(selected.LocalID) : QStringLiteral("源文件");
    Json::Value value; std::string error;
    if (ReadMeta(*meta, value, error)) description.Type = QString::fromStdString(value["ImporterType"].asString());
    const auto state = meta->GetImportState();
    description.State = state == eAssetImportState::Ready ? "就绪" : state == eAssetImportState::Failed ? "导入失败"
        : state == eAssetImportState::Pending ? "待导入" : "无需导入";
    if (auto* asset = Asset(services, target)) {
        description.Name = QString::fromStdString(asset->GetName());
        description.Type = AssetDisplayName(asset->GetAssetType());
        description.Diagnostics["ArtifactPath"] = asset->GetArtifactPath();
        description.Diagnostics["DataVersion"] = asset->GetDataVersion();
        description.Diagnostics["DataSize"] = Json::UInt64(asset->GetDataSize());
    } else description.Diagnostics["子资源数量"] = Json::UInt64(meta->GetSubAssets().size());
    return description;
}
}
