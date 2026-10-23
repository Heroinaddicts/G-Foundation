#ifndef COMPONENT_INTERFACE_H
#define COMPONENT_INTERFACE_H

#include "FoundationApi.h"

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace Foundation {
    inline void FoundationLog(FoundationApi* foundation, const int level, const bool console, const char* file, const int line, const char* format, ...) {
        if (!foundation || !format) {
            return;
        }

        va_list arguments;
        va_start(arguments, format);
        va_list argumentsCopy;
        va_copy(argumentsCopy, arguments);
        const int contentLength = std::vsnprintf(nullptr, 0, format, argumentsCopy);
        va_end(argumentsCopy);
        if (contentLength < 0) {
            va_end(arguments);
            return;
        }

        std::vector<char> content(static_cast<std::size_t>(contentLength) + 1);
        std::vsnprintf(content.data(), content.size(), format, arguments);
        va_end(arguments);
        foundation->LogAsync(level, content.data(), console, file, line);
    }

    class IFoundation {
    public:
        virtual ~IFoundation() {}

        virtual bool Initialize(FoundationApi* foundation) = 0;
        virtual bool Launch(FoundationApi* foundation) = 0;
        virtual void Release(FoundationApi* foundation) = 0;

        virtual void EarlyUpdate(FoundationApi* foundation) = 0;
        virtual void Update(FoundationApi* foundation) = 0;
        virtual void LaterUpdate(FoundationApi* foundation) = 0;
    };
}

#define XLog(foundation, level, format, ...) \
    ::Foundation::FoundationLog(foundation, static_cast<int>(level), true, __FILE__, __LINE__, format __VA_OPT__(,) __VA_ARGS__)
#define TraceLog(foundation, format, ...) \
    XLog(foundation, ::Foundation::eLogLevel::FoundationTrace, format __VA_OPT__(,) __VA_ARGS__)
#define ErrorLog(foundation, format, ...) \
    XLog(foundation, ::Foundation::eLogLevel::FoundationError, format __VA_OPT__(,) __VA_ARGS__)
#if defined(_DEBUG)
#define DEBUG(foundation, format, ...) \
    XLog(foundation, ::Foundation::eLogLevel::FoundationDebug, format __VA_OPT__(,) __VA_ARGS__)
#else
#define DEBUG(foundation, format, ...)
#endif

// Compatibility spelling used only by the preserved low-level Net implementation.
#define XLOG(foundation, format, ...) TraceLog(foundation, format __VA_OPT__(,) __VA_ARGS__)

#endif // COMPONENT_INTERFACE_H
