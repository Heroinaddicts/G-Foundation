#ifndef __SafeBit_h__
#define __SafeBit_h__

#include "MultiSys.hpp"
#include <vector>
#include <bitset>

namespace FoundationUtils {
    namespace SafeBit {
        static std::vector<bool>& operator<<(std::vector<bool>& bits, const bool bit) {
            bits.push_back(bit);
            return bits;
        }

        __forceinline std::vector<bool> CharToBits(Int8 c) {
            std::bitset<8> bitset(c);
            std::vector<bool> bits;
            for (size_t i = 0; i < 8; ++i) {
                bits.push_back(bitset[7 - i]);
            }
            return bits;
        }

        // �� Bits ת��Ϊ Int8
        __forceinline Int8 BitsToInt8(const std::vector<bool>& bits) {
            std::bitset<8> bitset;
            for (size_t i = 0; i < 8 && i < bits.size(); ++i) {
                bitset[7 - i] = bits[i];
            }
            return static_cast<Int8>(bitset.to_ulong());
        }

        // �� Int32 ת��Ϊ Bits
        __forceinline std::vector<bool> IntToBits(Int32 value) {
            std::bitset<32> bitset(value);
            std::vector<bool> bits;
            for (size_t i = 0; i < 32; ++i) {
                bits.push_back(bitset[31 - i]);
            }
            return bits;
        }

        // �� Bits ת��Ϊ Int32
        __forceinline Int32 BitsToInt(const std::vector<bool>& bits) {
            std::bitset<32> bitset;
            for (size_t i = 0; i < 32 && i < bits.size(); ++i) {
                bitset[31 - i] = bits[i];
            }
            return static_cast<Int32>(bitset.to_ulong());
        }

        // �� Int64 ת��Ϊ Bits
        __forceinline std::vector<bool> Int64ToBits(Int64 value) {
            std::bitset<64> bitset(value);
            std::vector<bool> bits;
            for (size_t i = 0; i < 64; ++i) {
                bits.push_back(bitset[63 - i]);
            }
            return bits;
        }

        // �� Bits ת��Ϊ Int64
        __forceinline Int64 BitsToInt64(const std::vector<bool>& bits) {
            std::bitset<64> bitset;
            for (size_t i = 0; i < 64 && i < bits.size(); ++i) {
                bitset[63 - i] = bits[i];
            }
            return static_cast<Int64>(bitset.to_ullong());
        }
    }
}

#endif //__SafeBit_h__


