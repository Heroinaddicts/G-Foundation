#include "EditorRuntimeResources.h"
#include "ILibraryManager.h"
#include "../../Module/Shared/AssetValueBinary.h"
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace {
bool Parse(const std::string& text, Json::Value& value) {
    Json::Reader reader;
    return reader.parse(text, value, false) && value.isObject();
}
bool Unsigned(const Json::Value& value, std::uint32_t& number) {
    if (!value.isUInt64() || value.asUInt64() > std::numeric_limits<std::uint32_t>::max()) return false;
    number = static_cast<std::uint32_t>(value.asUInt64());
    return true;
}
float FloatAt(const QByteArray& bytes, std::uint64_t offset) {
    return std::bit_cast<float>(qFromLittleEndian<std::uint32_t>(reinterpret_cast<const uchar*>(bytes.constData() + offset)));
}
bool ColorValue(const Json::Value& value, GameSproutRuntime::Color& color) {
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
bool ReferenceValue(const Json::Value& value, GameSproutRuntime::ResourceReference& reference) {
    if (!value.isObject() || !value["Guid"].isString() || !value["LocalID"].isUInt64()) return false;
    const Guid guid(value["Guid"].asString());
    if (!guid.IsValid() || value["LocalID"].asUInt64() == 0) return false;
    reference = {guid.GetGuidString(),value["LocalID"].asUInt64()};
    return true;
}
}

EditorRuntimeResources::EditorRuntimeResources(IAssetsManager* assets, ILibraryManager* library)
    : _Assets(assets), _Library(library) {
    if (_Assets) _Subscribed = _Assets->RegisterAssetListener(this);
}
EditorRuntimeResources::~EditorRuntimeResources() {
    if (_Subscribed) _Assets->UnregisterAssetListener(this);
}

bool EditorRuntimeResources::Read(const GameSproutRuntime::ResourceReference& reference, eAssetType type,
    Json::Value& descriptor, QByteArray& bytes, std::string& error) {
    error.clear(); bytes.clear(); descriptor = Json::Value();
    const Guid guid(reference.Guid);
    if (!_Library || !guid.IsValid() || reference.LocalID == 0) { error = "Invalid resource reference or unavailable LibraryManager"; return false; }
    IAsset* asset = _Library->QueryAsset(guid, reference.LocalID);
    if (!asset || asset->GetAssetType() != type) { error = "Prepared resource is missing or has the wrong type: " + reference.Guid + "_" + std::to_string(reference.LocalID); return false; }
    if (asset->GetDataVersion() != 2 || !Parse(asset->GetDataDescriptorJson(), descriptor)) { error = "Invalid ArtifactDB resource descriptor"; return false; }
    QFile file(QString::fromStdString(asset->GetArtifactPath()));
    // 单次预览分配上限，错误文件不能触发无限分配。
    if (!QFileInfo(file).isAbsolute() || !file.open(QIODevice::ReadOnly)
        || file.size() < 0 || static_cast<std::uint64_t>(file.size()) != asset->GetDataSize()
        || file.size() > 512 * 1024 * 1024) { error = "Cannot read prepared artifact: " + file.fileName().toStdString(); return false; }
    bytes = file.readAll();
    if (file.error() != QFileDevice::NoError || static_cast<std::uint64_t>(bytes.size()) != asset->GetDataSize()) {
        bytes.clear(); error = "Incomplete prepared artifact"; return false;
    }
    return true;
}

bool EditorRuntimeResources::LoadMesh(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::MeshRenderData& output, std::string& error) {
    output = {};
    Json::Value descriptor;
    QByteArray bytes;
    if (!Read(reference, eAssetType::Mesh, descriptor, bytes, error)) return false;
    GameSproutRuntime::MeshRenderData data;
    const auto fail = [&error]() { error = "Invalid prepared mesh layout or index range"; return false; };
    if (descriptor["Encoding"] != "RawBytes" || descriptor["IndexFormat"] != "UInt32"
        || !Unsigned(descriptor["VertexCount"], data.VertexCount) || data.VertexCount == 0
        || !Unsigned(descriptor["VertexStride"], data.VertexStride) || data.VertexStride == 0
        || !Unsigned(descriptor["IndexCount"], data.IndexCount) || data.IndexCount == 0
        || data.IndexCount % 3 != 0 || !descriptor["IndexOffset"].isUInt64()) return fail();
    data.IndexOffset = descriptor["IndexOffset"].asUInt64();
    const std::uint64_t vertexSize = std::uint64_t(data.VertexCount) * data.VertexStride;
    const std::uint64_t indexSize = std::uint64_t(data.IndexCount) * 4;
    if (data.IndexOffset != vertexSize || data.IndexOffset % 4 != 0 || data.VertexStride % 4 != 0
        || vertexSize > std::uint64_t(bytes.size()) || indexSize != std::uint64_t(bytes.size()) - vertexSize
        || !descriptor["VertexAttributes"].isArray()) return fail();
    bool position = false, normal = false;
    for (const Json::Value& attribute : descriptor["VertexAttributes"]) {
        if (!attribute.isObject() || !attribute["Semantic"].isString()) return fail();
        const std::string semantic = attribute["Semantic"].asString();
        const bool uv = semantic == "TexCoord" && attribute.get("Channel",0) == 0;
        if (semantic != "Position" && semantic != "Normal" && semantic != "Color" && !uv) continue;
        std::uint32_t offset = 0;
        const std::uint32_t size = semantic == "Color" ? 16 : (uv ? 8 : 12);
        if (!Unsigned(attribute["Offset"], offset) || offset > std::uint32_t(INT32_MAX) || offset % 4 != 0
            || offset > data.VertexStride || size > data.VertexStride - offset
            || attribute["Format"] != (semantic == "Color" ? "Float32x4" : (uv ? "Float32x2" : "Float32x3"))) return fail();
        if (semantic == "Position") { if (position) return fail(); position = true; data.PositionOffset = offset; }
        else if (semantic == "Normal") { if (normal) return fail(); normal = true; data.NormalOffset = offset; }
        else if (uv) { if (data.UVOffset >= 0) return fail(); data.UVOffset = static_cast<std::int32_t>(offset); }
        else { if (data.ColorOffset >= 0) return fail(); data.ColorOffset = static_cast<std::int32_t>(offset); }
    }
    if (!position || !normal || !descriptor["SubMeshes"].isArray() || descriptor["SubMeshes"].empty()) return fail();
    for (const Json::Value& section : descriptor["SubMeshes"]) {
        GameSproutRuntime::MeshSection part;
        if (!section.isObject() || !Unsigned(section["FirstIndex"],part.FirstIndex) || !Unsigned(section["IndexCount"],part.IndexCount)
            || !Unsigned(section["MaterialSlot"],part.MaterialSlot) || part.MaterialSlot >= 1024
            || part.IndexCount == 0 || part.IndexCount % 3 != 0 || part.FirstIndex % 3 != 0
            || part.FirstIndex > data.IndexCount || part.IndexCount > data.IndexCount - part.FirstIndex) return fail();
        data.Sections.push_back(part);
    }
    GameSproutRuntime::Vector3 minimum{std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
    GameSproutRuntime::Vector3 maximum{-minimum.X,-minimum.Y,-minimum.Z};
    for (std::uint32_t vertex = 0; vertex < data.VertexCount; ++vertex) {
        const std::uint64_t offset = std::uint64_t(vertex) * data.VertexStride + data.PositionOffset;
        const GameSproutRuntime::Vector3 point{FloatAt(bytes,offset),FloatAt(bytes,offset+4),FloatAt(bytes,offset+8)};
        if (!std::isfinite(point.X) || !std::isfinite(point.Y) || !std::isfinite(point.Z)) return fail();
        minimum = {std::min(minimum.X,point.X),std::min(minimum.Y,point.Y),std::min(minimum.Z,point.Z)};
        maximum = {std::max(maximum.X,point.X),std::max(maximum.Y,point.Y),std::max(maximum.Z,point.Z)};
    }
    for (std::uint32_t index = 0; index < data.IndexCount; ++index) {
        if (qFromLittleEndian<std::uint32_t>(reinterpret_cast<const uchar*>(bytes.constData() + data.IndexOffset + std::uint64_t(index)*4)) >= data.VertexCount) return fail();
    }
    data.LocalBounds = {minimum * 0.5f + maximum * 0.5f,maximum * 0.5f - minimum * 0.5f};
    data.Bytes.assign(reinterpret_cast<const std::uint8_t*>(bytes.constData()),reinterpret_cast<const std::uint8_t*>(bytes.constData()) + bytes.size());
    output = std::move(data);
    return true;
}

bool EditorRuntimeResources::LoadTexture(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::TextureRenderData& output, std::string& error) {
    output = {};
    Json::Value descriptor;
    QByteArray bytes;
    if (!Read(reference, eAssetType::Texture, descriptor, bytes, error)) return false;
    GameSproutRuntime::TextureRenderData data;
    const auto fail = [&error]() { error = "Invalid prepared RGBA8 texture layout or sampling settings"; return false; };
    if (descriptor["Encoding"] != "RawBytes" || descriptor["Format"] != "RGBA8"
        || !Unsigned(descriptor["Width"], data.Width) || !Unsigned(descriptor["Height"], data.Height)
        || data.Width == 0 || data.Height == 0 || !descriptor["Mips"].isArray() || descriptor["Mips"].empty()) return fail();
    const Json::Value& mip = descriptor["Mips"][0];
    if (std::uint64_t(data.Width) * 4 > std::uint64_t(bytes.size())
        || data.Height > std::uint64_t(bytes.size()) / (std::uint64_t(data.Width) * 4)) return fail();
    const std::uint64_t size = std::uint64_t(data.Width) * data.Height * 4;
    if (!mip.isObject() || !mip["Offset"].isUInt64() || !mip["Size"].isUInt64() || !mip["RowPitch"].isUInt64()
        || mip["Offset"].asUInt64() != 0 || mip["Size"].asUInt64() != size
        || mip["RowPitch"].asUInt64() != std::uint64_t(data.Width)*4 || size > std::uint64_t(bytes.size())
        || mip["Width"] != descriptor["Width"] || mip["Height"] != descriptor["Height"]) return fail();
    const Json::Value& settings = descriptor["Settings"];
    if (!settings.isObject()) return fail();
    for (const char* key : {"ColorSpace","FilterMode","WrapU","WrapV"})
        if (settings.isMember(key) && !settings[key].isString()) return fail();
    const std::string colorSpace = settings.get("ColorSpace","SRGB").asString();
    if (colorSpace != "SRGB" && colorSpace != "Linear") return fail();
    data.SRGB = colorSpace == "SRGB";
    const std::string filter = settings.get("FilterMode","Bilinear").asString();
    if (filter != "Point" && filter != "Bilinear" && filter != "Trilinear") return fail();
    data.Sampler.MinFilter = data.Sampler.MagFilter = filter == "Point" ? GRHI::FilterMode::Nearest : GRHI::FilterMode::Linear;
    const auto wrap = [](const std::string& value, GRHI::AddressMode& mode) {
        if (value == "Repeat") mode = GRHI::AddressMode::Repeat;
        else if (value == "ClampToEdge") mode = GRHI::AddressMode::ClampToEdge;
        else if (value == "MirrorRepeat") mode = GRHI::AddressMode::MirroredRepeat;
        else return false;
        return true;
    };
    if (!wrap(settings.get("WrapU","Repeat").asString(),data.Sampler.AddressU)
        || !wrap(settings.get("WrapV","Repeat").asString(),data.Sampler.AddressV)) return fail();
    data.Pixels.assign(reinterpret_cast<const std::uint8_t*>(bytes.constData()),reinterpret_cast<const std::uint8_t*>(bytes.constData()) + size);
    output = std::move(data);
    return true;
}

bool EditorRuntimeResources::LoadMaterial(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::MaterialRenderData& output, std::string& error) {
    output = {};
    Json::Value descriptor, body;
    QByteArray bytes;
    if (!Read(reference,eAssetType::Material,descriptor,bytes,error)) return false;
    const auto fail = [&error]() { error = "Invalid prepared material data"; return false; };
    if (descriptor["Encoding"] != "BinaryValueLE1" || !AssetValueBinary::Decode(bytes,body) || !body.isObject()) return fail();
    if (body["Format"] == "GameSproutDocument") body = Json::Value(body["Data"]);
    if (!body.isObject()) return fail();
    GameSproutRuntime::MaterialRenderData data;
    if (body.isMember("BaseColor") && !ColorValue(body["BaseColor"],data.BaseColor)) return fail();
    if (body.isMember("DoubleSided")) {
        if (!body["DoubleSided"].isBool()) return fail();
        data.DoubleSided = body["DoubleSided"].asBool();
    }
    if (body.isMember("BaseColorTexture") && !body["BaseColorTexture"].isNull()
        && !ReferenceValue(body["BaseColorTexture"],data.BaseColorTexture)) return fail();
    if (data.BaseColorTexture.Guid.empty() && body["Textures"].isArray()) {
        for (const Json::Value& texture : body["Textures"]) {
            if (!texture.isObject() || !texture["Channel"].isString()) continue;
            const std::string channel = texture["Channel"].asString();
            if (channel != "DiffuseColor" && channel != "BaseColor" && channel != "base_color" && channel != "baseColor") continue;
            if (!ReferenceValue(texture["Asset"],data.BaseColorTexture)) return fail();
            break;
        }
    }
    output = std::move(data);
    return true;
}
