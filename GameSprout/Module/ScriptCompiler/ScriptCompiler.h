#pragma once
#include "IScriptCompiler.h"
class ScriptCompiler final : public IScriptCompiler {
public:
    ScriptCompilerToolchain GetToolchain() const override;
    bool Compile(const ScriptCompileRequest&, ScriptCompileResult&,
                 const std::function<bool()>& cancelled) const override;
private:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override { return true; }
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override { return true; }
    void Release(GameSproutEditor::GameSproutModuleApi*) override {}
};
