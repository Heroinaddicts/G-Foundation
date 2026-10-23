#ifndef FOUNDATION_UTILS_SAFE_MEMORY_HPP
#define FOUNDATION_UTILS_SAFE_MEMORY_HPP

#include "MultiSys.hpp"

#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace FoundationUtils {
    namespace SafeMemory {
        inline constexpr std::size_t Kb = 1024;
        inline constexpr std::size_t Mb = 1024 * Kb;
        inline constexpr std::size_t Gb = 1024 * Mb;

        __forceinline void Memset(void* destination, const std::size_t destinationSize, const int value, const std::size_t size) {
            FoundationAssert(size <= destinationSize, "SafeMemory Memset overflow");
            std::memset(destination, value, size <= destinationSize ? size : destinationSize);
        }

        __forceinline void Memcpy(void* destination, const std::size_t destinationSize, const void* source, const std::size_t size) {
            FoundationAssert(size <= destinationSize, "SafeMemory Memcpy overflow");
            std::memcpy(destination, source, size <= destinationSize ? size : destinationSize);
        }

        __forceinline void Memmove(void* destination, const std::size_t destinationSize, const void* source, const std::size_t size) {
            FoundationAssert(size <= destinationSize, "SafeMemory Memmove overflow");
            std::memmove(destination, source, size <= destinationSize ? size : destinationSize);
        }

        __forceinline bool Strcmp(const char* destination, const char* source) {
            return std::strcmp(destination, source) == 0;
        }

        void* AlignedMalloc(std::size_t size, std::size_t alignment);
        void AlignedFree(void* memory);
    }
}

#if defined(FOUNDATION_UTILS_SAFE_MEMORY_BUILD)
#if defined(_WIN32)
#include <malloc.h>
#endif


namespace FoundationUtils {
    namespace SafeMemory {
        void* AlignedMalloc(const std::size_t size, const std::size_t alignment) {
#if defined(_WIN32)
            return _aligned_malloc(size, alignment);
#else
            void* memory = nullptr;
            return posix_memalign(&memory, alignment, size) == 0 ? memory : nullptr;
#endif
        }

        void AlignedFree(void* memory) {
#if defined(_WIN32)
            _aligned_free(memory);
#else
            std::free(memory);
#endif
        }

    }
}
#endif // defined(FOUNDATION_UTILS_SAFE_MEMORY_BUILD)

#endif // FOUNDATION_UTILS_SAFE_MEMORY_HPP


