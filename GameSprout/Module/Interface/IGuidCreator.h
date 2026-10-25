#ifndef __IGuidCreator_h__
#define __IGuidCreator_h__

#include "Define.h"
#include <cstdint>
#include <string>

struct Guid {
    const uint32_t _Part[4];

    constexpr Guid() : Guid(0, 0, 0, 0) {}

    constexpr Guid(const uint32_t part1, const uint32_t part2, const uint32_t part3, const uint32_t part4)
        : _Part{part1, part2, part3, part4} {}

    // Match Unity: malformed input produces the all-zero GUID.
    explicit Guid(const std::string & guidString) : Guid(Parse(guidString)) {}

    constexpr bool IsValid() const {
        return (_Part[0] | _Part[1] | _Part[2] | _Part[3]) != 0;
    }

    // Return by value so the GUID itself remains exactly 16 bytes.
    std::string GetGuidString() const {
        static constexpr char hex[] = "0123456789abcdef";
        std::string result(32, '0');
        for (size_t i = 0; i < 4; ++i) {
            for (size_t j = 0; j < 8; ++j) {
                // Unity writes the lowest hexadecimal digit first per UInt32.
                result[i * 8 + j] = hex[(_Part[i] >> (j * 4)) & 0xFu];
            }
        }
        return result;
    }

    constexpr bool operator==(const Guid & rhs) const {
        for (size_t i = 0; i < 4; ++i) {
            if (_Part[i] != rhs._Part[i])
                return false;
        }
        return true;
    }

    constexpr bool operator!=(const Guid & rhs) const {
        return !(*this == rhs);
    }

    constexpr bool operator<(const Guid & rhs) const {
        for (size_t i = 0; i < 4; ++i) {
            if (_Part[i] != rhs._Part[i])
                return _Part[i] < rhs._Part[i];
        }
        return false;
    }

private:
    static int HexValue(const unsigned char ch) {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    }

    static Guid Parse(const std::string & guidString) {
        if (guidString.size() != 32)
            return Guid();

        uint32_t parts[4] = {};
        for (size_t i = 0; i < 4; ++i) {
            for (size_t j = 0; j < 8; ++j) {
                const int digit = HexValue(
                    static_cast<unsigned char>(guidString[i * 8 + j]));
                if (digit < 0)
                    return Guid();
                parts[i] |= static_cast<uint32_t>(digit) << (j * 4);
            }
        }
        return Guid(parts[0], parts[1], parts[2], parts[3]);
    }
};

static_assert(sizeof(Guid) == 16, "Guid must remain a 128-bit value.");

class IGuidCreator : public GameSprout::IGameSproutModule {
public:
    virtual ~IGuidCreator() {}

    // Returns an immutable value. An all-zero GUID indicates generation failure.
    virtual Guid CreateGuid() = 0;
};

#endif //__IGuidCreator_h__
