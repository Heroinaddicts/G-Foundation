#pragma once
#include "IScriptEditor.h"

namespace ScriptFieldTree {
// Resolves both the current value and its declared schema. Bounds and kind
// checks happen on every command; a stale UI path cannot address another value.
bool Resolve(GameSproutRuntime::ScriptValue&,const GameSproutRuntime::ScriptFieldInfo&,
    const ScriptFieldPath&,GameSproutRuntime::ScriptValue*&,const GameSproutRuntime::ScriptFieldInfo*&,std::string&);
bool EditContainer(GameSproutRuntime::ScriptValue&,const GameSproutRuntime::ScriptFieldInfo&,
    const ScriptContainerEdit&,std::string&);
}
