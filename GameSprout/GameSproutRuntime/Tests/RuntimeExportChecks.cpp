#include "GameSproutRuntimeExport.h"
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <vector>

GameSproutRuntime::IGameSproutRuntime* LoadFromOtherTranslationUnit(const std::string& path);

static void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static int RunChecks(int argc, const std::string* argv) {
    GameSproutRuntime::IGameSproutRuntime* first = nullptr;
    GameSproutRuntime::IGameSproutRuntime* second = nullptr;
    try {
        Check(argc == 4, "Expected good/null/missing fixture directories");
        static_assert(std::is_same_v<decltype(&CreateGameSproutRuntime), fCreateGameSproutRuntime>);
        Check(!Export(""), "Empty path accepted");
        Check(!Export(std::string(argv[1]) + "/nonexistent"), "Missing library accepted");
        Check(!Export(std::string(argv[1]) + std::string("\0suffix", 7)), "Embedded NUL accepted");
        Check(!Export(argv[2]), "Null factory accepted");
        Check(!Export(argv[3]), "Missing symbol accepted");
        // 重复失败后成功加载，检查失败路径没有破坏后续调用。
        Check(!Export(argv[2]) && !Export(argv[3]), "Repeated failure changed result");
        first = Export(argv[1]);
        Check(first != nullptr && first->GetSceneCount() == 0, "Cannot load/call Runtime interface");
        first->Update(0.016f);
        const std::filesystem::path relative = std::filesystem::relative(std::filesystem::path(std::u8string(
            reinterpret_cast<const char8_t*>(argv[1].data()), argv[1].size())));
        const std::u8string utf8 = relative.u8string();
        const std::string relativePath(reinterpret_cast<const char*>(utf8.data()), utf8.size());
        second = LoadFromOtherTranslationUnit(relativePath + "/");
        Check(second && second != first, "Loader cached instance or failed in other translation unit");
        // 退出加载函数后仍可调用虚函数、delete，证明代码没有提前卸载。
        delete first; first = nullptr;
        Check(second->GetSceneCount() == 0 && !second->GetScene(0), "Other Runtime invalidated");
        delete second; second = nullptr;
        std::cout << "PASS dynamic loading, UTF-8/relative/trailing paths, missing library/symbol, null factory, multiple instances/TUs and destruction\n";
        return 0;
    } catch (const std::exception& error) {
        delete first;
        delete second;
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) {
    // Windows 的普通 main(argv) 可能使用本地代码页；测试必须明确传递 UTF-8。
    std::vector<std::string> arguments;
    for (int i = 0; i < argc; ++i) {
        const int length = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, nullptr, 0, nullptr, nullptr);
        if (length <= 0) return 1;
        std::string utf8(static_cast<std::size_t>(length), '\0');
        if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, utf8.data(), length, nullptr, nullptr) != length) return 1;
        utf8.pop_back();
        arguments.push_back(std::move(utf8));
    }
    return RunChecks(argc, arguments.data());
}
#else
int main(int argc, char** argv) {
    const std::vector<std::string> arguments(argv, argv + argc);
    return RunChecks(argc, arguments.data());
}
#endif
