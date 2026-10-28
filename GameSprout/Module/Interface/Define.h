#ifndef __Define_h__
#define __Define_h__

#include "GameSproutApi.h"
#include "GRHIApi.h"

enum class eAssetType : uint16_t {
    Unknown = 0,
    Texture = 1,
    Audio = 2,
    Mesh = 3,
    Model = 4,
    Material = 5,
    Shader = 6,
    ComputeShader = 7,
    ShaderInclude = 8,
    Font = 9,
    Video = 10,
    Text = 11,
    Script = 12,
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
    Folder = 254,
    Prefab = 255 // 暂不注册和处理。
};

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

enum class eMeshNormalsMode : uint8_t {
    Import,
    Calculate,
    None,
};

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

enum class eModelAnimationType : uint8_t {
    None,
    Generic,
    Humanoid,
};

enum class eShaderSourceLanguage : uint8_t {
    Auto,
    HLSL,
    GLSL,
    MSL,
    SPIRV,
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
