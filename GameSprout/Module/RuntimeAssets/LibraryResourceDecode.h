#pragma once
#include "LibraryRuntimeResources.h"
#include "../Shared/AssetValueBinary.h"
#include "../Shared/AssetArtifactIO.h"
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <set>
namespace LibraryResourceDecode {
inline bool Parse(const std::string& text, Json::Value& value) {
    return AssetArtifactIO::Parse(text,value) && value.isObject();
}
inline bool Unsigned(const Json::Value& value, std::uint32_t& number) {
    if (!value.isUInt64() || value.asUInt64() > std::numeric_limits<std::uint32_t>::max()) return false;
    number = static_cast<std::uint32_t>(value.asUInt64());
    return true;
}
inline float FloatAt(const QByteArray& bytes, std::uint64_t offset) {
    return std::bit_cast<float>(qFromLittleEndian<std::uint32_t>(reinterpret_cast<const uchar*>(bytes.constData() + offset)));
}
inline bool ColorValue(const Json::Value& value, GameSproutRuntime::Color& color) {
    if (!value.isArray() || (value.size() != 3 && value.size() != 4)) return false;
    float channels[4] = {1,1,1,1};
    for (Json::ArrayIndex index = 0; index < value.size(); ++index) {
        if (!value[index].isNumeric() || !std::isfinite(value[index].asDouble())) return false;
        channels[index] = value[index].asFloat();
        if (!std::isfinite(channels[index])) return false;
    }
    color = {channels[0],channels[1],channels[2],channels[3]};
    return true;
}
inline bool ReferenceValue(const Json::Value& value, GameSproutRuntime::ResourceReference& reference) {
    if (!value.isObject() || !value["Guid"].isString() || !value["LocalID"].isUInt64()) return false;
    const Guid guid(value["Guid"].asString());
    if (!guid.IsValid() || value["LocalID"].asUInt64() == 0) return false;
    reference = {guid.GetGuidString(),value["LocalID"].asUInt64()};
    return true;
}
inline bool FloatArray(const Json::Value& value, float* output, unsigned count) {
    if (!value.isArray() || value.size()!=count) return false;
    for (unsigned index=0;index<count;++index) {
        if (!value[index].isNumeric() || !std::isfinite(value[index].asDouble())) return false;
        output[index]=value[index].asFloat();
        if (!std::isfinite(output[index])) return false;
    }
    return true;
}
inline bool PoseValue(const Json::Value& value, GameSproutRuntime::TransformPose& pose) {
    float position[3],rotation[4],scale[3];
    if (!value.isObject() || !FloatArray(value["Translation"],position,3)
        || !FloatArray(value["Rotation"],rotation,4) || !FloatArray(value["Scale"],scale,3)) return false;
    pose.Position={position[0],position[1],position[2]};
    pose.Rotation={rotation[0],rotation[1],rotation[2],rotation[3]};
    if (!pose.Rotation.TryNormalize(pose.Rotation)) return false;
    pose.Scale={scale[0],scale[1],scale[2]};
    return true;
}
inline bool MatrixValue(const Json::Value& value, GameSproutRuntime::Mat4& matrix) {
    float numbers[12];
    if (!FloatArray(value,numbers,12)) return false;
    for (unsigned column=0;column<4;++column)
        for (unsigned row=0;row<3;++row) matrix(row,column)=numbers[column*3+row];
    return true;
}
}
