#pragma once
#include <stdexcept>

namespace GameSproutRuntime {
class IGameObject;
namespace Internal {
#if defined(_WIN32)
#define GAMESPROUT_SCRIPT_CONSTRUCTION_LOCAL
#else
#define GAMESPROUT_SCRIPT_CONSTRUCTION_LOCAL __attribute__((visibility("hidden")))
#endif
// Runtime factories run on the owning host thread. Each game module has its
// own context; nested factories restore it even when construction throws.
// Avoid native module TLS here: Darwin can retain an image containing TLV
// storage after dlclose, which prevents loading new code into a reused slot.
GAMESPROUT_SCRIPT_CONSTRUCTION_LOCAL inline IGameObject*& ScriptConstructionOwner() {
    static IGameObject* owner=nullptr;
    return owner;
}
GAMESPROUT_SCRIPT_CONSTRUCTION_LOCAL inline IGameObject* RequireScriptConstructionOwner() {
    auto* owner=ScriptConstructionOwner();
    if(!owner) throw std::logic_error("Script components must be created through GameObject/Runtime");
    return owner;
}
class GAMESPROUT_SCRIPT_CONSTRUCTION_LOCAL ScriptConstructionScope {
public:
    explicit ScriptConstructionScope(IGameObject* owner):_Previous(ScriptConstructionOwner()) {ScriptConstructionOwner()=owner;}
    ~ScriptConstructionScope(){ScriptConstructionOwner()=_Previous;}
    ScriptConstructionScope(const ScriptConstructionScope&)=delete;
    ScriptConstructionScope& operator=(const ScriptConstructionScope&)=delete;
private:
    IGameObject* const _Previous;
};
}
}
