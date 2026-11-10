#pragma once
#include "IScriptCompiler.h"
#include "ScriptBuildStorage.h"
#include <QByteArray>
#include <QString>

// Uses compiler-produced AST JSON. It does not parse declarations from C++ text
// and never creates an instance of a user's class to discover defaults.
class ScriptBindingsGenerator final {
public:
    static bool Generate(const ScriptCompileRequest& request,const QString& sourceRoot,
                         const std::vector<ScriptBuildStorage::SourceSnapshot>& sources,
                         const QString& probePath,QByteArray& generated,
                         std::vector<GameSproutRuntime::ScriptTypeInfo>& types,
                         std::vector<GameSproutRuntime::GameModuleTypeInfo>& modules,
                         std::string& diagnostics,std::string& warnings,
                         const std::function<bool()>& cancelled);
};
