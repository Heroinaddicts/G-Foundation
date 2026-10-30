#pragma once
#include "GameSproutApi.h"
#include "Define.h"
#include <vector>

enum class eShaderCodeFormat : uint8_t { SPIRV, MSL, DXIL, Metallib };
enum class eShaderPlatform : uint8_t { Vulkan, Direct3D12, Apple, MacOS, IOS, IOSSimulator };

struct ShaderCompileTarget {
    eShaderCodeFormat Format = eShaderCodeFormat::SPIRV;
    eShaderPlatform Platform = eShaderPlatform::Vulkan;
};
struct ShaderCompileRequest {
    std::string SourceRoot; // Allowed include root, normally project/Assets.
    std::string SourcePath; // Relative to SourceRoot, used in diagnostics.
    std::string Source;     // Snapshot of UTF-8 HLSL, borrowed only during Compile.
    std::string WorkDirectory; // Caller-owned temporary directory, never Library/Artifact.
    eShaderStage Stage = eShaderStage::Vertex;
    std::string EntryPoint = "main";
    std::vector<std::string> Defines;
    bool Optimize = true;
    bool GenerateDebugInfo = false;
    std::vector<ShaderCompileTarget> Targets = {
        {eShaderCodeFormat::SPIRV,eShaderPlatform::Vulkan},
        {eShaderCodeFormat::MSL,eShaderPlatform::Apple}};
    std::function<bool()> IsCancellationRequested; // Called synchronously on the compiling thread.
};
struct ShaderCompiledCode {
    eShaderCodeFormat Format = eShaderCodeFormat::SPIRV;
    eShaderPlatform Platform = eShaderPlatform::Vulkan;
    std::string EntryPoint;
    std::vector<uint8_t> Data; // MSL includes its terminating NUL.
};
struct ShaderCompileDependency {
    std::string Path; // Relative to SourceRoot.
    std::string ContentHash; // SHA256 of the exact include bytes used by this compilation.
};
struct ShaderCompileResult {
    bool Success = false;
    std::string Error;
    std::string Diagnostics;
    std::string ReflectionJson = "{}"; // Description only; never prepended to code.
    std::vector<ShaderCompiledCode> Codes;
    std::vector<ShaderCompileDependency> Dependencies;
};

class IShaderCompiler : public GameSprout::IGameSproutModule {
public:
    virtual ~IShaderCompiler() {}
    virtual const std::string & GetToolchainHash() const = 0;
    // Blocking, cancellation-aware, reentrant after Initialize. No UI, DB, GUID,
    // GPU objects or task submission. Request callbacks are never retained.
    virtual bool Compile(const ShaderCompileRequest & request, ShaderCompileResult & result) const = 0;
};
