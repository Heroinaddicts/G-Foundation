#pragma once
#include "GameSproutRuntimeApi.h"
#include "Internal/ScriptConstruction.h"

namespace GameSproutRuntime {
// Runtime owns construction, lifetime and the associated GameObject. User
// classes only implement lifecycle callbacks and declare their gameplay data.
class ScriptBehaviour : public IScript {
public:
    const std::type_info& GetComponentType() const override {return typeid(*this);}
    const char* GetTypeName() const override {return "GameSproutRuntime.NativeBehaviour";}
protected:
    GAMESPROUT_SCRIPT_CONSTRUCTION_LOCAL ScriptBehaviour():IScript(Internal::RequireScriptConstructionOwner()) {}
};
namespace Internal {
template<typename T> struct ScriptMarker {};
template<typename T> struct ScriptFieldAccess;
}
}
// The compiler recognizes this declaration marker and generates a dedicated
// accessor for the declaring class. Access stays private/protected in user code.
#if defined(__clang__)
#define GAMESPROUT_SERIALIZE_FIELD \
    template<typename> friend struct ::GameSproutRuntime::Internal::ScriptFieldAccess; \
    [[clang::annotate("GameSprout.SerializeField")]]
#else
#define GAMESPROUT_SERIALIZE_FIELD \
    template<typename> friend struct ::GameSproutRuntime::Internal::ScriptFieldAccess;
#endif
#define GAMESPROUT_SCRIPT_JOIN_IMPL(a,b) a##b
#define GAMESPROUT_SCRIPT_JOIN(a,b) GAMESPROUT_SCRIPT_JOIN_IMPL(a,b)
// A declaration marker for offline C++ reflection. It performs no registration
// and embeds no persistent identity or field bridge in user source files.
#define GAMESPROUT_SCRIPT(Type) \
    namespace { using GAMESPROUT_SCRIPT_JOIN(GameSproutScriptMarker_,__COUNTER__) = ::GameSproutRuntime::Internal::ScriptMarker<Type>; }
