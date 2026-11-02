#pragma once

#include "ResourceReference.h"
#include <cstdint>
#include <string>
#include <vector>

namespace GameSproutRuntime {

// Prepared, platform-neutral descriptions. Runtime never compiles source HLSL.
// First graphics contract: one Forward pass, canonical float P/N/C/UV inputs at
// locations 0/1/2/3, fragment Texture2D/sampler pairs, scalar/vector material
// constants. Skinned user-Shader variants and arbitrary pipeline injection are
// separate capabilities, not silently substituted with the preview Shader.
enum class ShaderProgramStage : std::uint8_t { Vertex, Fragment };
enum class ShaderProgramFormat : std::uint8_t { SPIRV, MSL, DXIL, Metallib };
enum class ShaderProgramPlatform : std::uint8_t { Vulkan, Direct3D12, Apple, MacOS, IOS, IOSSimulator };
enum class ShaderValueType : std::uint8_t { Float, Int, UInt, Float2, Float3, Float4 };
enum class ShaderUniformSource : std::uint8_t { Draw, Material };
enum class ShaderCullMode : std::uint8_t { Off, Front, Back };
enum class ShaderBlendMode : std::uint8_t { Opaque, Alpha, PremultipliedAlpha };
enum class ShaderDepthCompare : std::uint8_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };

struct ShaderUniformMember {
    std::string Name;
    ShaderValueType Type=ShaderValueType::Float;
    std::uint32_t Offset=0;
};
struct ShaderUniformBlock {
    std::uint32_t Slot=0,Size=0;
    // Draw is vertex b0/space1: column-major Model, ViewProjection, NormalMatrix
    // (offsets 0/64/128, total 192 bytes). Material blocks use reflected members;
    // Size is padded to 16 bytes, retaining the compiler's member offsets.
    ShaderUniformSource Source=ShaderUniformSource::Material;
    std::vector<ShaderUniformMember> Members;
};
struct ShaderTextureBinding {
    std::string Name; // Matches a Texture material property; null uses white.
    std::uint32_t Slot=0;
};
struct ShaderProgramCode {
    ShaderProgramFormat Format=ShaderProgramFormat::SPIRV;
    ShaderProgramPlatform Platform=ShaderProgramPlatform::Vulkan;
    std::string EntryPoint;
    std::vector<std::uint8_t> Bytes; // MSL includes its terminating NUL.
};
struct ShaderProgramData {
    ShaderProgramStage Stage=ShaderProgramStage::Vertex;
    std::vector<ShaderProgramCode> Codes;
    std::vector<ShaderUniformBlock> Uniforms;
    std::vector<ShaderTextureBinding> Textures;
};
struct ShaderPassData {
    std::string Name="Forward",LightMode="Forward";
    ShaderProgramData Vertex;
    ShaderProgramData Fragment{ShaderProgramStage::Fragment,{},{},{}};
    ShaderCullMode Cull=ShaderCullMode::Back;
    ShaderBlendMode Blend=ShaderBlendMode::Opaque;
    bool DepthTest=true,DepthWrite=true;
    ShaderDepthCompare DepthCompare=ShaderDepthCompare::LessEqual;
};
struct ShaderRenderData {
    std::string Name;
    std::vector<ShaderPassData> Passes;
};
struct MaterialPropertyData {
    std::string Name;
    ShaderValueType Type=ShaderValueType::Float;
    // Scalar float/vector values use Numbers; Int and UInt preserve integer bits.
    float Numbers[4]{};
    std::int32_t Integer=0;
    std::uint32_t Unsigned=0;
};
struct MaterialTextureData {
    std::string Name;
    ResourceReference Texture;
};

}
