#ifndef __Define_h__
#define __Define_h__

#include "GameSproutEditorApi.h"
#include "GRHIApi.h"

#include <cstdint>

// 源文件采用哪一类导入规则，由 AssetMeta / AssetsPipeline 使用。
// 分类依据是处理流程，不是具体后缀：FBX、OBJ、glTF 都可以采用 Model。
// 一个导入器可以产生多个、不同 eAssetType 的资源对象。
enum class eAssetImporterType : uint16_t {
    Unknown = 0,

    // 图片、音频、模型等外部源数据。
    Texture = 1,
    Audio = 2,
    Model = 4,
    Font = 9,
    Video = 10,
    Text = 11,

    // Shader、依赖文件和脚本的处理规则。
    Shader = 6,
    ComputeShader = 7,
    ShaderInclude = 8,
    Script = 12,

    // 已按引擎格式保存的资源文件，也需要相应的读取和预处理规则。
    Mesh = 3,
    Material = 5,
    Scene = 13,
    AnimationClip = 14,
    AnimatorController = 15,
    AnimatorOverrideController = 16,
    AvatarMask = 17,
    AudioMixer = 18,
    PhysicsMaterial = 19,
    PhysicsMaterial2D = 20,
    RenderTexture = 21,
    Cubemap = 22,
    SpriteAtlas = 23,
    TerrainData = 24,
    TerrainLayer = 25,
    ShaderVariantCollection = 26,
    DataAsset = 27,
    LightingSettings = 28,
    LightmapParameters = 29,

    Folder = 254, // 目录的身份和 Meta 管理，不生成可渲染的模型等产物。
    Prefab = 255  // 预留，暂不注册和处理。
};

// 一个可独立引用的资源对象是什么，由 IAsset 及其具体资源接口使用。
// 资源对象通过源文件 GUID + LocalID 标识；主资源也有自己的对象类型。
// Model 表示模型层级及其资源引用描述，不表示该文件中所有对象都是 Model。
// 两个枚举各自独立，不能通过 static_cast 或数值相等来推断对应关系。
// 例如 Model 导入器可以产生 Model、Mesh、Material、AnimationClip、Avatar。
// Meta 与源文件工厂使用 eAssetImporterType，不使用本枚举判断后缀。
enum class eAssetType : uint16_t {
    Unknown = 0,

    // 几何、表面及其他基础资源。
    Texture = 1,
    Mesh = 3,
    Model = 4,
    Material = 5,
    Shader = 6,
    ComputeShader = 7,
    Font = 9,
    RenderTexture = 21,
    Cubemap = 22,
    SpriteAtlas = 23,
    Sprite = 32, // 图片的一个可独立引用区域，与 Texture 对象分开。

    // 动画和骨骼；一个 Model 可以包含多个动画片段。
    Skeleton = 30, // 骨骼层级、绑定姿态等骨架描述。
    Avatar = 31,   // 通用或人形动画使用的骨架映射描述。
    AnimationClip = 14,
    AnimatorController = 15,
    AnimatorOverrideController = 16,
    AvatarMask = 17,

    // 音视频和数据资源。
    Audio = 2, // 音频片段资源。
    AudioMixer = 18,
    Video = 10,
    Text = 11,
    Script = 12,
    DataAsset = 27,

    // 场景、物理、地形和光照相关资源。
    Scene = 13,
    Prefab = 255, // 预留，暂不注册和处理。
    PhysicsMaterial = 19,
    PhysicsMaterial2D = 20,
    TerrainData = 24,
    TerrainLayer = 25,
    ShaderVariantCollection = 26,
    LightingSettings = 28,
    LightmapParameters = 29,

    // 编辑器侧资源类别，不作为可执行 Shader 或游戏运行时目录产物。
    ShaderInclude = 8,
    Folder = 254
};

// 以下枚举是导入参数或默认资源属性，不承担源文件/资源对象的类型识别。

// 图片表示普通颜色，还是线性数值。
enum class eTextureColorSpace : uint8_t {
    SRGB,
    Linear
};

// 是否使用原图的透明通道。
enum class eTextureAlphaSource : uint8_t {
    None,
    FromSource
};

// 运行时如何取贴图颜色。
enum class eTextureFilterMode : uint8_t {
    Point,       // 最近像素，适合清晰的像素风。
    Bilinear,    // 混合邻近像素。
    Trilinear    // 同时平滑相邻 Mip 层之间的过渡。
};

// 纹理坐标超出图片边界后的处理方式。
enum class eTextureWrapMode : uint8_t {
    Repeat,      // 重复平铺。
    ClampToEdge, // 使用边缘颜色。
    MirrorRepeat // 镜像重复。
};

enum class eAudioLoadType : uint8_t {
    DecompressOnLoad,
    CompressedInMemory,
    Streaming,
};

enum class eAudioCompressionFormat : uint8_t {
    PCM,
    Vorbis,
    ADPCM,
};

enum class eAudioSampleRateMode : uint8_t {
    Preserve,
    Optimize,
    Override,
};

// 法线的来源；Calculate 只表示重新计算，不表示使用哪一种计算算法。
enum class eMeshNormalsMode : uint8_t {
    Import,
    Calculate,
    None,
};

// 切线的来源；具体计算算法由后续网格处理设置定义。
enum class eMeshTangentsMode : uint8_t {
    Import,
    Calculate,
    None,
};

enum class eMeshCompression : uint8_t {
    Off,
    Low,
    Medium,
    High,
};

// 模型的骨骼动画处理方式，不是 AnimationClip 等资源对象的类型。
enum class eModelAnimationType : uint8_t {
    None,
    Generic,
    Humanoid,
};

enum class eShaderStage : uint8_t {
    Vertex,
    Fragment,
    Compute
};

enum class eShaderSourceLanguage : uint8_t {
    HLSL
};

enum class eFontRenderMode : uint8_t {
    Smooth,
    Raster,
};

enum class eVideoCodec : uint8_t {
    Auto,
    H264,
    H265,
    VP8,
};

enum class eTextEncoding : uint8_t {
    Auto,
    UTF8,
    UTF16LE,
    UTF16BE,
    Binary,
};

#endif //__Define_h__
