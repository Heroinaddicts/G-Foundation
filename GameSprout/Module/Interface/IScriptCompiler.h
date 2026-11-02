#pragma once
#include "GameSproutEditorApi.h"

struct ScriptCompileSource {
    std::string Path; // Relative to Assets; never a .meta file.
    std::string SourceGuid;
};
struct ScriptCompileRequest {
    std::string AssetsRoot;
    std::string BuildRoot;
    std::string ArtifactRoot;
    std::string AssemblyGuid;
    std::vector<ScriptCompileSource> Sources;
    // Worker-thread wait for another Editor/IDE process using this project's
    // fixed build tree. Zero fails immediately; waiting remains cancellable.
    int LockWaitMilliseconds = 0;
};
struct ScriptCompilerToolchain {
    std::string RuntimeSDKDirectory;
    std::string CMakeProgram;
    std::string CompilerProgram;
    std::string Configuration;
    std::string Architectures;
    std::string Sysroot;
    std::string DeploymentTarget;
    std::string BaseFlags;
    std::string ConfigurationFlags;
};
struct ScriptCompileResult {
    std::string LibraryPath;
    std::string LibraryHash; // SHA256 of the verified candidate; loader rechecks while staging.
    std::string Fingerprint;
    std::string Diagnostics;
    std::string ReflectionWarnings;
    // Pure value metadata; available without loading/executing GameLogic.
    std::vector<GameSproutRuntime::ScriptTypeInfo> Types;
    std::string SchemaPath;
    std::string SchemaHash;
    bool CacheHit = false;
};
class IScriptCompiler : public GameSproutEditor::IGameSproutModule {
public:
    // Value snapshot used by external editor adapters for the same SDK/indexing
    // settings. The compiler still owns actual compilation and reflection.
    virtual ScriptCompilerToolchain GetToolchain() const { return {}; }
    // Blocking compiler operation. ScriptEditor calls it on a TaskApi worker.
    // Sources and SDK are included in the fingerprint; source .meta belongs to AssetsManager.
    virtual bool Compile(const ScriptCompileRequest& request, ScriptCompileResult& result,
                         const std::function<bool()>& cancelled) const = 0;
};
