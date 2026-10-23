#ifndef __GRHIExport_h__
#define __GRHIExport_h__

#include "GRHIApi.h"


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
#error GRHI dynamic loading does not support the current platform.
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

// A successful load keeps the module loaded until process exit, so returned
// objects and their virtual functions remain valid. Repeated successful calls
// share one initialized API instance. One designated caller deletes it only
// after all devices and other users have finished. Access is on the main thread.
// path is the library directory; GRHI chooses the platform-specific filename.
inline GRHI::GRHIApi* ExportGRHIApi(const std::string& path) {
    if (path.empty()) {
        return nullptr;
    }

#if defined(_WIN32)
    const char* libraryName = "GRHI.dll";
#elif defined(__APPLE__)
    const char* libraryName = "libGRHI.dylib";
#else
    const char* libraryName = "libGRHI.so";
#endif
    const bool hasTrailingSeparator = path.back() == '/' || path.back() == '\\';
    const std::string libraryPath =
        path + (hasTrailingSeparator ? "" : "/") + libraryName;

    using CreateFunction = GRHI::GRHIApi* (CALL*)() noexcept;

#if defined(_WIN32)
    // Resolve a UTF-8 directory to an absolute Windows path. Dependencies such
    // as SDL3.dll must be searched beside GRHI.dll, including for external hosts.
    const int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        libraryPath.c_str(), -1, nullptr, 0);
    if (wideLength <= 0) {
        return nullptr;
    }
    std::wstring widePath(static_cast<size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, libraryPath.c_str(),
        -1, widePath.data(), wideLength) != wideLength) {
        return nullptr;
    }
    const DWORD fullLength = GetFullPathNameW(widePath.c_str(), 0, nullptr, nullptr);
    if (fullLength == 0) {
        return nullptr;
    }
    std::wstring fullPath(static_cast<size_t>(fullLength), L'\0');
    const DWORD written = GetFullPathNameW(widePath.c_str(), fullLength, fullPath.data(), nullptr);
    if (written == 0 || written >= fullLength) {
        return nullptr;
    }
    HMODULE library = LoadLibraryExW(fullPath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (library == nullptr) {
        return nullptr;
    }

    auto create = reinterpret_cast<CreateFunction>(
        GetProcAddress(library, "CreateFoundation")
    );
    if (create == nullptr) {
        FreeLibrary(library);
        return nullptr;
    }
#else
    void* library = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) {
        return nullptr;
    }

    auto create = reinterpret_cast<CreateFunction>(
        dlsym(library, "CreateFoundation")
    );
    if (create == nullptr) {
        dlclose(library);
        return nullptr;
    }
#endif

    GRHI::GRHIApi* api = create();
    if (api == nullptr) {
#if defined(_WIN32)
        FreeLibrary(library);
#else
        dlclose(library);
#endif
    }

    return api;
}

#endif //__GRHIExport_h__
