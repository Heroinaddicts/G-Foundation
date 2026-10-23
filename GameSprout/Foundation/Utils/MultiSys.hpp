#ifndef FOUNDATION_UTILS_MULTI_SYS_HPP
#define FOUNDATION_UTILS_MULTI_SYS_HPP

#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <typeinfo>
#include <vector>

#define UsingFoundation using namespace Foundation

#if defined(_WIN32)
#pragma execution_character_set("utf-8")
#else
#ifndef __forceinline
#define __forceinline inline __attribute__((always_inline))
#endif
#ifndef sprintf_s
#define sprintf_s std::snprintf
#endif
#endif

using Int8 = std::int8_t;
using Int16 = std::int16_t;
using Int32 = std::int32_t;
using Int64 = std::int64_t;
using UInt8 = std::uint8_t;
using UInt16 = std::uint16_t;
using UInt32 = std::uint32_t;
using UInt64 = std::uint64_t;

#define OUT
#define IN

inline void __FoundationAssert__(const char* file, const int line, const char* functionName, const char* format, ...) {
    char content[256] = {};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(content, sizeof(content), format, arguments);
    va_end(arguments);

    std::fprintf(stderr, "FoundationAssert: %s:%s:%d\nInfo: %s\n", file, functionName, line, content);
    std::fflush(stderr);
    assert(false);
}

#define txmalloc std::malloc
#define txfree std::free
#define txnew new
#define txdel delete

#if defined(_WIN32)
#define tx_aligned_malloc _aligned_malloc
#define tx_aligned_free _aligned_free
#else
#define tx_aligned_malloc std::aligned_alloc
#define tx_aligned_free std::free
#endif

#if defined(_DEBUG) || !defined(NDEBUG)
#define FoundationAssert(condition, format, ...) do { if (!(condition)) { __FoundationAssert__(__FILE__, __LINE__, __FUNCTION__, format __VA_OPT__(,) __VA_ARGS__); } } while (false)
#else
#define FoundationAssert(condition, format, ...) do { static_cast<void>(sizeof(condition)); } while (false)
#endif

#define xmin(x, y) ((x) > (y) ? (y) : (x))
#define xmax(x, y) ((x) > (y) ? (x) : (y))

#endif // FOUNDATION_UTILS_MULTI_SYS_HPP


