#pragma once

#include <cstdint>
#include <string>

namespace GameSproutRuntime {
    enum class RuntimeLogLevel : std::uint8_t {
        Debug,
        Trace,
        Warning,
        Error
    };

    struct RuntimeLogRecord {
        RuntimeLogLevel Level;
        std::string Message;
        std::string File;
        std::uint_least32_t Line;
    };

    class IRuntimeLogListener {
    public:
        virtual ~IRuntimeLogListener() = default;
        // Delivered on the Runtime owner thread. Copy the record to retain it.
        virtual void OnRuntimeLog(const RuntimeLogRecord& record) = 0;
    };
}
