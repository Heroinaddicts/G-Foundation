#pragma once
#include "IScriptCompiler.h"
#include <QByteArray>

// Tooling metadata only. Native callbacks/addresses never enter this document.
namespace ScriptSchemaDocument {
QByteArray Serialize(const std::string& buildID,const std::vector<GameSproutRuntime::ScriptTypeInfo>& types);
bool Parse(const QByteArray& bytes,const std::string& buildID,
           std::vector<GameSproutRuntime::ScriptTypeInfo>& types,std::string& error);
}
