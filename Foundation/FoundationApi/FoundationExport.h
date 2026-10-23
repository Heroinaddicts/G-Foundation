#ifndef FoundationExport_H
#define FoundationExport_H

#include "FoundationApi.h"

#include <filesystem>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#else
#error Foundation dynamic loading does not support the current platform.
#endif

#if defined(_WIN32)
#define CALL __cdecl
#if defined(EXPORTS)
#define API __declspec(dllexport)
#else
#define API __declspec(dllimport)
#endif
#else
#define CALL
#define API __attribute__((visibility("default")))
#endif

namespace Foundation {
    static_assert(sizeof(void*) == 8, "Foundation requires a 64-bit target.");

    using fCreateFoundation = FoundationApi* (CALL*)() noexcept;
    using fReleaseFoundation = void (CALL*)(FoundationApi*) noexcept;

    inline FoundationApi* GetFoundationApi(const std::string& path) noexcept {
        if (path.empty()) {
            return nullptr;
        }

#if defined(_WIN32)
        const char* const libraryName = "Foundation.dll";
#elif defined(__APPLE__)
        const char* const libraryName = "libFoundation.dylib";
#elif defined(__linux__)
        const char* const libraryName = "libFoundation.so";
#else
#error Foundation dynamic loading does not support the current platform.
#endif

        const bool hasTrailingSeparator = path.back() == '/' || path.back() == '\\';
        const std::string libraryPath = path + (hasTrailingSeparator ? "" : "/") + libraryName;

#if defined(_WIN32)
        const std::filesystem::path nativeLibraryPath(std::u8string(
            reinterpret_cast<const char8_t*>(libraryPath.data()),
            libraryPath.size()
        ));
        HMODULE const library = ::LoadLibraryW(nativeLibraryPath.c_str());
        if (!library) {
            return nullptr;
        }

        FARPROC const createSymbol = ::GetProcAddress(library, "CreateFoundation");
        FARPROC const releaseSymbol = ::GetProcAddress(library, "ReleaseFoundation");
        if (!createSymbol || !releaseSymbol) {
            ::FreeLibrary(library);
            return nullptr;
        }

        const fCreateFoundation createFoundation = reinterpret_cast<fCreateFoundation>(createSymbol);
        FoundationApi* const foundation = createFoundation();
        if (!foundation) {
            ::FreeLibrary(library);
            return nullptr;
        }
#else
        void* const library = ::dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!library) {
            return nullptr;
        }

        void* const createSymbol = ::dlsym(library, "CreateFoundation");
        void* const releaseSymbol = ::dlsym(library, "ReleaseFoundation");
        if (!createSymbol || !releaseSymbol) {
            ::dlclose(library);
            return nullptr;
        }

        const fCreateFoundation createFoundation = reinterpret_cast<fCreateFoundation>(createSymbol);
        FoundationApi* const foundation = createFoundation();
        if (!foundation) {
            ::dlclose(library);
            return nullptr;
        }
#endif

        // The returned object's virtual table and implementations live in the
        // loaded library. Foundation does not support hot unload, so the successful
        // library handle intentionally remains valid for the process lifetime.
        return foundation;
    }
}

extern "C" API Foundation::FoundationApi* CALL CreateFoundation() noexcept;
extern "C" API void CALL ReleaseFoundation(Foundation::FoundationApi* foundation) noexcept;

#endif // FoundationExport_H
