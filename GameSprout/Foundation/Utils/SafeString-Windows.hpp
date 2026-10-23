#if defined(_WIN32)
#include "SafeString.hpp"
#include <Shlwapi.h>
#include <Windows.h>
#pragma comment(lib, "shlwapi.lib")

namespace FoundationUtils {
    std::string SafeString::UTF8ToGBK(const std::string& src) {
        if (src.empty()) {
            return "";
        }

        int wideLength = MultiByteToWideChar(CP_UTF8, 0, src.c_str(), -1, nullptr, 0);
        if (wideLength <= 0) {
            return "";
        }

        std::wstring wide;
        wide.resize(static_cast<size_t>(wideLength));
        MultiByteToWideChar(CP_UTF8, 0, src.c_str(), -1, wide.data(), wideLength);

        int gbkLength = WideCharToMultiByte(936, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (gbkLength <= 0) {
            return "";
        }

        std::string gbk;
        gbk.resize(static_cast<size_t>(gbkLength));
        WideCharToMultiByte(936, 0, wide.c_str(), -1, gbk.data(), gbkLength, nullptr, nullptr);

        if (!gbk.empty() && gbk.back() == '\0') {
            gbk.pop_back();
        }

        return gbk;
    }

    std::wstring SafeString::UnicodeEscapeToWString(const std::string& input) {
        std::wstring result;
        for (size_t i = 0; i < input.length(); ++i) {
            if (input[i] == '\\' && i + 1 < input.length() && input[i + 1] == 'u') {
                unsigned int codePoint = 0;
                sscanf(input.substr(i + 2, 4).c_str(), "%x", &codePoint);
                result += static_cast<wchar_t>(codePoint);
                i += 5;
            }
            else {
                result += static_cast<wchar_t>(input[i]);
            }
        }
        return result;
    }
}
#endif // defined(_WIN32)


