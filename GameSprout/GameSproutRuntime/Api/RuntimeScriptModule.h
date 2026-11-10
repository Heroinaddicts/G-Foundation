#pragma once
#include "GameSproutRuntimeApi.h"
#include "Internal/ScriptConstruction.h"
#include <cstdarg>
#include <cstdio>

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
template<typename T> struct ModuleMarker {};
template<typename T> struct ScriptFieldAccess;
template<typename T> struct GameplayFactory;

#if defined(__clang__) || defined(__GNUC__)
[[gnu::format(printf, 4, 5)]]
#endif
inline void LogFormatted(IGameSproutRuntime* runtime, RuntimeLogLevel level,
                         std::source_location location, const char* format, ...) noexcept {
    if (!runtime || !format) return;

    char buffer[256];
    std::va_list arguments;
    va_start(arguments, format);
    const int length = std::vsnprintf(buffer, sizeof buffer, format, arguments);
    va_end(arguments);
    if (length < 0) return;

    const auto messageLength = static_cast<std::size_t>(length);
    if (messageLength < sizeof buffer) {
        runtime->Log(level, std::string_view(buffer, messageLength), location);
        return;
    }

    try {
        std::string message(messageLength + 1, '\0');
        va_start(arguments, format);
        const int written = std::vsnprintf(message.data(), message.size(), format, arguments);
        va_end(arguments);
        if (written >= 0 && static_cast<std::size_t>(written) < message.size()) {
            runtime->Log(level, std::string_view(message.data(), static_cast<std::size_t>(written)), location);
        }
    } catch (...) {}
}
}
}

#define GS_DEBUG(runtime, ...) \
    ::GameSproutRuntime::Internal::LogFormatted((runtime), ::GameSproutRuntime::RuntimeLogLevel::Debug, ::std::source_location::current(), __VA_ARGS__)
#define GS_TRACE(runtime, ...) \
    ::GameSproutRuntime::Internal::LogFormatted((runtime), ::GameSproutRuntime::RuntimeLogLevel::Trace, ::std::source_location::current(), __VA_ARGS__)
#define GS_ERROR(runtime, ...) \
    ::GameSproutRuntime::Internal::LogFormatted((runtime), ::GameSproutRuntime::RuntimeLogLevel::Error, ::std::source_location::current(), __VA_ARGS__)

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

// The GameLogic build generates a factory from this declaration. Instances
// belong to a Runtime gameplay session.
#define GAMESPROUT_REGIST_MODULE(Type) \
    namespace { using GAMESPROUT_SCRIPT_JOIN(GameSproutModuleMarker_,__COUNTER__) = ::GameSproutRuntime::Internal::ModuleMarker<Type>; }
