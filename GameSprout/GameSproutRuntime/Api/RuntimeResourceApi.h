#pragma once
#include "GameSproutRuntimeApi.h"
#include "RuntimeAnimationData.h"
#include "GRHIApi.h"

namespace GameSproutRuntime {

struct MeshSection {
    std::uint32_t FirstIndex = 0;
    std::uint32_t IndexCount = 0;
    std::uint32_t MaterialSlot = 0;
};

// 已预处理的顶点与 UInt32 索引原始字节，直接上传 GRHI Buffer。
// 顶点属性均为 float32：Position/Normal 三分量、Color 四分量、UV 两分量。
struct MeshRenderData {
    std::vector<std::uint8_t> Bytes;
    std::uint32_t VertexCount = 0;
    std::uint32_t VertexStride = 0;
    std::uint32_t IndexCount = 0;
    std::uint64_t IndexOffset = 0;
    std::uint32_t PositionOffset = 0;
    std::uint32_t NormalOffset = 12;
    std::int32_t ColorOffset = -1;
    std::int32_t UVOffset = -1;
    std::int32_t BoneIndicesOffset = -1;
    std::int32_t BoneWeightsOffset = -1;
    std::vector<SkinBinding> Bones;
    std::vector<MeshSection> Sections;
    Bounds LocalBounds;
};

struct TextureRenderData {
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    bool SRGB = true;
    // 第一版上传 mip0；此处已是紧密排列的 RGBA8，无源图片解码。
    std::vector<std::uint8_t> Pixels;
    GRHI::SamplerDesc Sampler;
};

struct MaterialRenderData {
    Color BaseColor;
    ResourceReference BaseColorTexture;
    Vector4 BaseColorTextureST{1,1,0,0}; // XY 为平铺倍率，ZW 为 UV 偏移。
    bool DoubleSided = false;
};

class IRuntimeResourceProvider {
public:
    virtual ~IRuntimeResourceProvider() = default;
    // 返回递增修订号，产物替换/删除时令 Renderer 释放并重新查询缓存。
    virtual std::uint64_t GetRevision() const = 0;
    // 同步读取已预处理数据；失败清空输出并给出原因。不得返回源 FBX/PNG。
    virtual bool LoadMesh(const ResourceReference& reference, MeshRenderData& data, std::string& error) = 0;
    virtual bool LoadTexture(const ResourceReference& reference, TextureRenderData& data, std::string& error) = 0;
    virtual bool LoadMaterial(const ResourceReference& reference, MaterialRenderData& data, std::string& error) = 0;
    virtual bool LoadModel(const ResourceReference&, ModelData& data, std::string& error) { data={};error="Model loading is unavailable";return false; }
    virtual bool LoadSkeleton(const ResourceReference&, SkeletonData& data, std::string& error) { data={};error="Skeleton loading is unavailable";return false; }
    virtual bool LoadAnimationClip(const ResourceReference&, AnimationClipData& data, std::string& error) { data={};error="AnimationClip loading is unavailable";return false; }
    virtual bool LoadAnimatorController(const ResourceReference&, AnimatorControllerData& data, std::string& error) { data={};error="AnimatorController loading is unavailable";return false; }
    virtual bool LoadHumanoidAvatar(const ResourceReference&, HumanoidAvatarData& data, std::string& error) {
        data={};error="Humanoid Avatar provider is unavailable";return false;
    }
    virtual bool LoadAvatarMask(const ResourceReference&, AvatarMaskData& data, std::string& error) { data={};error="AvatarMask loading is unavailable";return false; }
};

}
