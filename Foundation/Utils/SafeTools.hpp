#ifndef __SafeTools_h__
#define __SafeTools_h__

#include "MultiSys.hpp"
#include "SafeSystem.hpp"

namespace FoundationUtils {
    namespace SafeTools {
        template <typename T, size_t(*fun)(const T& src)>
        struct T_Hash {
            size_t operator()(const T& src) const {
                return fun(src);
            }
        };

        template <typename T, bool(*fun)(const T& src, const T& dst)>
        struct T_Compare {
            bool operator()(const T& src, const T& dst) const {
                return fun(src, dst);
            }
        };

        static UInt64 s_Seed = 0;
        inline void SetSeed(const UInt64 seed) {
            s_Seed = seed;
        }

        inline void SetSeed() {
            SetSeed(SafeSystem::Time::GetMilliSecond());
        }

        inline UInt32 Rand(UInt32 range = 0xffff) {
            if (0 == range) {
                return 0;
            }
            if (s_Seed == 0) {
                s_Seed = SafeSystem::Time::GetMicroSecond();
            }

            s_Seed = (((s_Seed = s_Seed * 214013L + 2531011L) >> 16) & 0x7fff);
            return s_Seed % range;
        }

        std::string Base64Encode(const std::string& src);
        std::string Base64Decode(const std::string& src);
    }
}

#if defined(FOUNDATION_UTILS_SAFE_TOOLS_BUILD)
#include <bitset>

namespace FoundationUtils {
    namespace SafeTools {
        const std::string s_Base64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string Base64Encode(const std::string& src) {
            std::string encode;
            int paddingCount = 0;

            // ���� padding ����
            int len = src.length();
            if (len % 3 != 0) {
                paddingCount = 3 - (len % 3);
            }

            for (int i = 0; i < len; i += 3) {
                int a = static_cast<int>(src[i]);
                int b = (i + 1 < len) ? static_cast<int>(src[i + 1]) : 0;
                int c = (i + 2 < len) ? static_cast<int>(src[i + 2]) : 0;

                int value = (a << 16) | (b << 8) | c;
                for (int j = 0; j < 4; j++) {
                    int index = (value >> (18 - j * 6)) & 0x3f;
                    encode += s_Base64Chars[index];
                }
            }

            // ��� padding
            for (int i = 0; i < paddingCount; i++) {
                encode += '=';
            }

            return encode;
        }

        std::string Base64Decode(const std::string& src) {
            std::string decode;
            int paddingCount = 0;

            // ���� padding ����
            int len = src.length();
            if (len > 0 && src[len - 1] == '=') {
                paddingCount++;
            }
            if (len > 1 && src[len - 2] == '=') {
                paddingCount++;
            }

            // ���ζ��ĸ��ַ�һ����н���
            for (int i = 0; i < len; i += 4) {
                int a = s_Base64Chars.find(src[i]);
                int b = (i + 1 < len) ? s_Base64Chars.find(src[i + 1]) : 0;
                int c = (i + 2 < len) ? s_Base64Chars.find(src[i + 2]) : 0;
                int d = (i + 3 < len) ? s_Base64Chars.find(src[i + 3]) : 0;

                int value = (a << 18) | (b << 12) | (c << 6) | d;

                for (int j = 0; j < 3 && (i + j * 3) < (len - paddingCount); j++) {
                    char ch = static_cast<char>((value >> (16 - j * 8)) & 0xff);
                    decode += ch;
                }
            }

            return decode;
        }
    }
}
#endif // defined(FOUNDATION_UTILS_SAFE_TOOLS_BUILD)

#endif //__SafeTools_h__


