#pragma once
#include "RuntimePhysicsData.h"
#include "GameSproutRuntimeApi.h"
#include "RuntimeAnimationData.h"
#include "RuntimeShaderData.h"
#include "Memory/ByteBuffer.h"
#include <span>

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

// Borrowed prepared vertex and UInt32 index bytes. Only positions and indices
// are read when building an optional native collision artifact.
struct MeshCollisionBakeInput {
    std::span<const std::uint8_t> Bytes;
    std::uint32_t VertexCount = 0;
    std::uint32_t VertexStride = 0;
    std::uint32_t IndexCount = 0;
    std::uint32_t PositionOffset = 0;
    std::uint64_t IndexOffset = 0;
};
struct CookedMeshCollisionData {
    std::vector<std::uint8_t> Triangle, Convex;
};

enum class TextureFilter : std::uint8_t { Nearest, Linear };
enum class TextureAddressMode : std::uint8_t { Repeat, ClampToEdge, MirroredRepeat };
struct SamplerSettings {
    TextureFilter MinFilter = TextureFilter::Linear, MagFilter = TextureFilter::Linear;
    TextureAddressMode AddressU = TextureAddressMode::ClampToEdge, AddressV = TextureAddressMode::ClampToEdge;
};
struct TextureRenderData {
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    bool SRGB = true;
    // 第一版上传 mip0；此处已是紧密排列的 RGBA8，无源图片解码。
    // Owning bulk byte storage. Rebuild all SDK consumers after this API layout
    // change; the prepared RGBA8 file format itself is unchanged.
    ByteBuffer Pixels;
    SamplerSettings Sampler;
};

struct FontGlyph {
    std::uint32_t Codepoint = 0;
    std::uint32_t X = 0, Y = 0, Width = 0, Height = 0; // Atlas pixels, top-left origin.
    float BearingX = 0, BearingY = 0; // Offset from baseline to bitmap top-left; Y is down.
    float AdvanceX = 0;
};
struct FontRenderData {
    ResourceReference Atlas;
    float PixelSize = 0, Ascent = 0, Descent = 0;
    std::vector<FontGlyph> Glyphs;
};

enum class MaterialShadingModel : std::uint8_t { Preview, Lit };
struct MaterialRenderData {
    Color BaseColor;
    ResourceReference BaseColorTexture;
    Vector4 BaseColorTextureST{1,1,0,0}; // XY 为平铺倍率，ZW 为 UV 偏移。
    bool DoubleSided = false;
    // Permission for Runtime's automatic grouping, never a requirement to use
    // an instanced shader. Unsupported meshes/passes keep their normal draws.
    bool EnableGPUInstancing = true;
    // Applied within the opaque/transparent category; higher values draw later.
    std::int32_t SortingPriority = 0; // [-50,50].
    MaterialShadingModel ShadingModel = MaterialShadingModel::Preview;
    // Built-in Lit uses linear RGB/HDR colors, as defined by Color. Roughness
    // is perceptual roughness [0,1]; Metallic is [0,1]. Normal maps are linear
    // tangent-space RGB with +Y convention and use their own UV transform.
    float Metallic = 0;
    float Roughness = 0.5f;
    bool SpecularHighlights = true; // Built-in Lit's direct-light specular term.
    ResourceReference NormalTexture;
    Vector4 NormalTextureST{1,1,0,0};
    float NormalScale = 1;
    Color EmissionColor{0,0,0,1};
    // Optional environment-art shading. Zero keeps the existing Lit result;
    // authoring controls never change instancing, geometry or shadow casting.
    float Stylization = 0;
    Color StylizedShadowTint{.65f,.75f,1,1};
    float LightThreshold = .35f, LightSoftness = .25f;
    float ColorSaturation = 1;
    // Empty Shader selects the engine's existing built-in preview material.
    ResourceReference Shader;
    std::vector<MaterialPropertyData> Properties;
    std::vector<MaterialTextureData> Textures;
};

class IRuntimeResourceProvider {
public:
    virtual ~IRuntimeResourceProvider() = default;
    // Provider 的变化序号；用于判断是否需要核对已缓存资源，不表示全部资源失效。
    virtual std::uint64_t GetRevision() const = 0;
    // 同一 Guid + LocalID 的有效数据或加载依赖变化时递增；Missing 的删除/恢复也须可区分。
    // 精确版本 Provider 的已登记资源查询只读内存。仅提供整体版本的 Provider 可沿用默认粒度。
    virtual std::uint64_t GetResourceRevision(const ResourceReference&) const { return GetRevision(); }
    // 同步读取已预处理数据；失败清空输出并给出原因。不得返回源 FBX/PNG。
    virtual bool LoadMesh(const ResourceReference& reference, MeshRenderData& data, std::string& error) = 0;
    virtual bool LoadTexture(const ResourceReference& reference, TextureRenderData& data, std::string& error) = 0;
    virtual bool LoadMaterial(const ResourceReference& reference, MaterialRenderData& data, std::string& error) = 0;
    virtual bool LoadShader(const ResourceReference&,ShaderRenderData& data,std::string& error) {
        data={};error="Shader loading is unavailable";return false;
    }
    virtual bool LoadPhysicsMaterial(const ResourceReference&,PhysicsMaterialData& data,std::string& error) {
        data={};error="Physics material loading is unavailable";return false;
    }
    virtual bool LoadModel(const ResourceReference&, ModelData& data, std::string& error) { data={};error="Model loading is unavailable";return false; }
    virtual bool LoadPrefab(const ResourceReference&, PrefabData& data, std::string& error) { data={};error="Prefab loading is unavailable";return false; }
    virtual bool LoadSkeleton(const ResourceReference&, SkeletonData& data, std::string& error) { data={};error="Skeleton loading is unavailable";return false; }
    virtual bool LoadAnimationClip(const ResourceReference&, AnimationClipData& data, std::string& error) { data={};error="AnimationClip loading is unavailable";return false; }
    virtual bool LoadAnimatorController(const ResourceReference&, AnimatorControllerData& data, std::string& error) { data={};error="AnimatorController loading is unavailable";return false; }
    virtual bool LoadHumanoidAvatar(const ResourceReference&, HumanoidAvatarData& data, std::string& error) {
        data={};error="Humanoid Avatar provider is unavailable";return false;
    }
    virtual bool LoadAvatarMask(const ResourceReference&, AvatarMaskData& data, std::string& error) { data={};error="AvatarMask loading is unavailable";return false; }
    // Prepared glyph metrics and texture identity. LoadTexture loads the atlas.
    virtual bool LoadFont(const ResourceReference&, FontRenderData& data, std::string& error) {
        data={};error="Font loading is unavailable";return false;
    }
    // Success with empty bytes means this mesh has no native collision artifact
    // for the requested shape type. Runtime can still build from LoadMesh.
    virtual bool LoadMeshCollision(const ResourceReference&, bool convex,
                                   std::vector<std::uint8_t>& bytes, std::string& error) {
        bytes.clear();error.clear();return true;
    }
};

}
