#ifndef __GameSproutRuntimeExport_h__
#define __GameSproutRuntimeExport_h__

#include "GameSproutRuntimeApi.h"
#include <filesystem>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#define GAMESPROUT_RUNTIME_CALL __cdecl
#if defined(GAMESPROUT_RUNTIME_EXPORTS)
#define GAMESPROUT_RUNTIME_API __declspec(dllexport)
#else
#define GAMESPROUT_RUNTIME_API __declspec(dllimport)
#endif
#elif defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#define GAMESPROUT_RUNTIME_CALL
#define GAMESPROUT_RUNTIME_API __attribute__((visibility("default")))
#else
#error GameSproutRuntime dynamic loading does not support this platform.
#endif

#define CreateGameSproutRuntimeFunctionName "CreateGameSproutRuntime"

typedef GameSproutRuntime::IGameSproutRuntime*
    (GAMESPROUT_RUNTIME_CALL *fCreateGameSproutRuntime)() noexcept;

// 由动态库实现；使用独立宏，避免和 Foundation/GRHI 的 API、CALL 宏冲突。
// 工厂不得让异常跨过此入口；失败返回 nullptr。
extern "C" GAMESPROUT_RUNTIME_API GameSproutRuntime::IGameSproutRuntime*
    GAMESPROUT_RUNTIME_CALL CreateGameSproutRuntime() noexcept;

// dlPath 是动态库所在目录，不是文件名；允许 UTF-8、相对目录和末尾分隔符。
// 宿主线程调用。失败返回 nullptr，释放本次取得的库引用。
// 成功后库保持加载至进程退出，使对象的虚函数、析构和内部代码一直有效。
// 返回对象由调用方在停止使用其场景/对象/组件后 delete，不支持热卸载。
// 这里只加载并调用工厂，不擅自缓存 Runtime 实例。
inline GameSproutRuntime::IGameSproutRuntime* Export(const std::string& dlPath) noexcept {
    if (dlPath.empty() || dlPath.find('\0') != std::string::npos) return nullptr;

    try {
#if defined(_WIN32)
        const char* const libraryName = "GameSproutRuntime.dll";
#elif defined(__APPLE__)
        const char* const libraryName = "libGameSproutRuntime.dylib";
#else
        const char* const libraryName = "libGameSproutRuntime.so";
#endif
        // 先转换为绝对路径。Windows 的依赖搜索选项要求 DLL 路径为绝对路径。
        const std::filesystem::path directory(std::u8string(
            reinterpret_cast<const char8_t*>(dlPath.data()), dlPath.size()));
        const std::filesystem::path libraryPath =
            std::filesystem::absolute(directory) / libraryName;

#if defined(_WIN32)
        HMODULE const library = ::LoadLibraryExW(libraryPath.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!library) return nullptr;
        const fCreateGameSproutRuntime create = reinterpret_cast<fCreateGameSproutRuntime>(
            ::GetProcAddress(library, CreateGameSproutRuntimeFunctionName));
        if (!create) {
            ::FreeLibrary(library);
            return nullptr;
        }
#else
        void* const library = ::dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!library) return nullptr;
        const fCreateGameSproutRuntime create = reinterpret_cast<fCreateGameSproutRuntime>(
            ::dlsym(library, CreateGameSproutRuntimeFunctionName));
        if (!create) {
            ::dlclose(library);
            return nullptr;
        }
#endif
        GameSproutRuntime::IGameSproutRuntime* const runtime = create();
        if (!runtime) {
#if defined(_WIN32)
            ::FreeLibrary(library);
#else
            ::dlclose(library);
#endif
        }
        return runtime;
    } catch (...) {
        // 路径转换/内存分配在加载模块之前完成，异常时尚未取得库引用。
        return nullptr;
    }
}

#endif //__GameSproutRuntimeExport_h__
