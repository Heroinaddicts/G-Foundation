#include "LibraryRuntimeResources.h"
#include "LibraryResourceDecode.h"
using LibraryResourceDecode::Unsigned;
using LibraryResourceDecode::FloatAt;
using LibraryResourceDecode::ColorValue;
using LibraryResourceDecode::ReferenceValue;
bool LibraryRuntimeResources::LoadMesh(const GameSproutRuntime::ResourceReference& reference,
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
        const bool indices=semantic=="BoneIndices",weights=semantic=="BoneWeights";
        if (semantic != "Position" && semantic != "Normal" && semantic != "Color" && !uv && !indices && !weights) continue;
        std::uint32_t offset = 0;
        const std::uint32_t size = semantic == "Color" || indices || weights ? 16 : (uv ? 8 : 12);
        if (!Unsigned(attribute["Offset"], offset) || offset > std::uint32_t(INT32_MAX) || offset % 4 != 0
            || offset > data.VertexStride || size > data.VertexStride - offset
            || attribute["Format"] != (indices ? "UInt32x4" : (semantic == "Color" || weights ? "Float32x4" : (uv ? "Float32x2" : "Float32x3")))) return fail();
        if (semantic == "Position") { if (position) return fail(); position = true; data.PositionOffset = offset; }
        else if (semantic == "Normal") { if (normal) return fail(); normal = true; data.NormalOffset = offset; }
        else if (uv) { if (data.UVOffset >= 0) return fail(); data.UVOffset = static_cast<std::int32_t>(offset); }
        else if(indices) {if(data.BoneIndicesOffset>=0)return fail();data.BoneIndicesOffset=static_cast<std::int32_t>(offset);}
        else if(weights) {if(data.BoneWeightsOffset>=0)return fail();data.BoneWeightsOffset=static_cast<std::int32_t>(offset);}
        else { if (data.ColorOffset >= 0) return fail(); data.ColorOffset = static_cast<std::int32_t>(offset); }
    }
    if ((data.BoneIndicesOffset>=0)!=(data.BoneWeightsOffset>=0)) return fail();
    if (descriptor.isMember("Bones")) {
        if(!descriptor["Bones"].isArray() || descriptor["Bones"].size()>65536)return fail();
        std::set<std::string> paths;
        for(const auto& value:descriptor["Bones"]) {
            GameSproutRuntime::SkinBinding binding;
            if(!value["NodePath"].isString() || value["NodePath"].asString().empty()
                || !paths.insert(value["NodePath"].asString()).second
                || !LibraryResourceDecode::MatrixValue(value["InverseBindMatrix"],binding.InverseBindMatrix))return fail();
            binding.NodePath=value["NodePath"].asString();data.Bones.push_back(std::move(binding));
        }
    }
    if(!data.Bones.empty() && data.BoneIndicesOffset<0)return fail();
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
        if(!data.Bones.empty()) {
            float sum=0;
            for(unsigned influence=0;influence<4;++influence) {
                const auto index=qFromLittleEndian<std::uint32_t>(reinterpret_cast<const uchar*>(bytes.constData()+
                    std::uint64_t(vertex)*data.VertexStride+data.BoneIndicesOffset+influence*4));
                const float weight=FloatAt(bytes,std::uint64_t(vertex)*data.VertexStride+data.BoneWeightsOffset+influence*4);
                if(!std::isfinite(weight)||weight<0||index>=data.Bones.size())return fail();
                sum+=weight;
            }
            if(std::fabs(sum-1.0f)>0.001f)return fail();
        }
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
