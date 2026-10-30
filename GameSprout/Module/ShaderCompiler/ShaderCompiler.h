#pragma once
#include "IShaderCompiler.h"
#include <QString>

struct ShaderCompilerTools {
    QString DXC;
    QString Cross;
    QString Reflect;
};
class ShaderCompiler final : public IShaderCompiler {
public:
    const std::string & GetToolchainHash() const override { return _ToolchainHash; }
    bool Compile(const ShaderCompileRequest &, ShaderCompileResult &) const override;
private:
    bool Initialize(GameSproutEditor::GameSproutModuleApi *) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi *) override { return true; }
    // Configuration stays immutable until outstanding Pipeline tasks have stopped.
    void Release(GameSproutEditor::GameSproutModuleApi *) override {}
    ShaderCompilerTools _Tools;
    std::string _ToolchainHash;
};
