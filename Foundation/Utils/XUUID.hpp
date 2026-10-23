#ifndef __XUUID_h__
#define __XUUID_h__

#include <iomanip>
#include <random>
#include <sstream>
namespace FoundationUtils {
    __forceinline std::string GenerateHEX(const int length) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);

        std::stringstream ss;
        for (int i = 0; i < length; ++i) {
            ss << std::hex << dis(gen);
        }
        return ss.str();
    }

    __forceinline std::string GenerateUUID() {
        std::string uuid = GenerateHEX(8) + "-" +
            GenerateHEX(4) + "-" +
            GenerateHEX(4) + "-" +
            GenerateHEX(4) + "-" +
            GenerateHEX(12);
        return uuid;
    }
}

#endif //__XUUID_h__


