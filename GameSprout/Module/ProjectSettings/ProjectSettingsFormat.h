#pragma once
#include "RuntimeProjectSettings.h"
#include <JsonCpp/json.hpp>
#include <QByteArray>
#include <sstream>
#include <set>

namespace ProjectSettingsFormat {
inline bool Parse(const QByteArray& bytes, Json::Value& value, std::string& error) {
    Json::CharReaderBuilder builder;
    builder["rejectDupKeys"] = true;
    builder["failIfExtra"] = true;
    builder["allowComments"] = false;
    std::istringstream input(bytes.toStdString());
    return Json::parseFromStream(builder, input, &value, &error);
}
inline QByteArray Encode(const GameSproutRuntime::RuntimeProjectSettings& data) {
    Json::Value root(Json::objectValue);
    root["Version"] = 1;
    root["NextTagID"] = Json::UInt64(data.NextTagID);
    root["Layers"] = Json::Value(Json::arrayValue);
    for (const auto& name : data.LayerNames) root["Layers"].append(name);
    root["Tags"] = Json::Value(Json::arrayValue);
    for (const auto& tag : data.Tags) {
        Json::Value value(Json::objectValue);
        value["ID"] = Json::UInt64(tag.ID);
        value["Name"] = tag.Name;
        root["Tags"].append(std::move(value));
    }
    root["CollisionMatrix"] = Json::Value(Json::arrayValue);
    for (const auto mask : data.CollisionMatrix) root["CollisionMatrix"].append(Json::UInt(mask));
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    writer["emitUTF8"] = true;
    return QByteArray::fromStdString(Json::writeString(writer, root) + "\n");
}
inline bool Decode(const QByteArray& bytes, GameSproutRuntime::RuntimeProjectSettings& output, std::string& error) {
    Json::Value root;
    if (!Parse(bytes, root, error)) return false;
    const std::set<std::string> fields{"Version", "NextTagID", "Layers", "Tags", "CollisionMatrix"};
    if (!root.isObject() || root["Version"] != 1 || !root["NextTagID"].isUInt64() ||
        !root["Layers"].isArray() || root["Layers"].size() != 32 ||
        !root["Tags"].isArray() || !root["CollisionMatrix"].isArray() || root["CollisionMatrix"].size() != 32) {
        error = "项目设置必须使用完整的 Version=1 当前格式。"; return false;
    }
    for (const auto& name : root.getMemberNames()) if (!fields.count(name)) {
        error = "项目设置包含未知字段：" + name; return false;
    }
    GameSproutRuntime::RuntimeProjectSettings result;
    result.NextTagID = root["NextTagID"].asUInt64();
    result.Tags.clear();
    for (Json::ArrayIndex i = 0; i < 32; ++i) {
        if (!root["Layers"][i].isString() || !root["CollisionMatrix"][i].isUInt()) {
            error = "Layer 名称必须为字符串，碰撞矩阵每行必须为 32 位无符号整数。"; return false;
        }
        result.LayerNames[i] = root["Layers"][i].asString();
        result.CollisionMatrix[i] = root["CollisionMatrix"][i].asUInt();
    }
    for (const auto& tag : root["Tags"]) {
        if (!tag.isObject() || tag.size() != 2 || !tag["ID"].isUInt64() || !tag["Name"].isString()) {
            error = "Tag 必须包含 ID 和 Name。"; return false;
        }
        result.Tags.push_back({tag["ID"].asUInt64(), tag["Name"].asString()});
    }
    if (!GameSproutRuntime::ValidateProjectSettings(result, error)) return false;
    output = std::move(result); return true;
}
}
