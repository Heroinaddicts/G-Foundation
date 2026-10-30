#include <cstring>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#elif defined(_WIN32)
#include <objbase.h>
#elif defined(__unix__) || defined(__ANDROID__)
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#else
#error Guid generation requires a supported system GUID or random source.
#endif

#include "GuidCreator.h"

namespace {
bool GenerateGuidParts(uint32_t (&parts)[4]) {
#if defined(__APPLE__)
    CFUUIDRef uuid = CFUUIDCreate(kCFAllocatorDefault);
    if (!uuid)
        return false;

    const CFUUIDBytes bytes = CFUUIDGetUUIDBytes(uuid);
    CFRelease(uuid);
    static_assert(sizeof(bytes) == sizeof(parts));
    std::memcpy(parts, &bytes, sizeof(parts));
#elif defined(_WIN32)
    GUID uuid;
    if (FAILED(::CoCreateGuid(&uuid)))
        return false;

    static_assert(sizeof(uuid) == sizeof(parts));
    std::memcpy(parts, &uuid, sizeof(parts));
#else
    // Use system entropy directly instead of a shared pseudo-random generator.
    int descriptor;
    do {
        descriptor = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0)
        return false;

    auto * bytes = reinterpret_cast<unsigned char *>(parts);
    size_t remaining = sizeof(parts);
    while (remaining > 0) {
        const ssize_t count = ::read(descriptor, bytes, remaining);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            ::close(descriptor);
            return false;
        }
        bytes += count;
        remaining -= static_cast<size_t>(count);
    }
    ::close(descriptor);

    // Unity stores the GUID variant in the low byte of its third UInt32.
    parts[2] = (parts[2] & ~0xC0u) | 0x80u;
#endif

    return (parts[2] & 0xC0u) == 0x80u;
}
}

Guid GuidCreator::CreateGuid() {
    uint32_t parts[4] = {};
    if (!GenerateGuidParts(parts))
        return Guid();
    return Guid(parts[0], parts[1], parts[2], parts[3]);
}

void GuidCreator::BeforeInitialize(GameSproutModuleApi * const api) {
    g_Self = this;
}

bool GuidCreator::Initialize(GameSproutModuleApi * const api) {
    g_GameSproutModuleApi = api;
    return true;
}

bool GuidCreator::Launch(GameSproutModuleApi * const api)  {
    return true;
}

void GuidCreator::LaunchFinished(GameSproutModuleApi * const api)  {

}

void GuidCreator::Update(GameSproutModuleApi * const api)  {
    
}

void GuidCreator::Release(GameSproutModuleApi * const api)  {
    
}
