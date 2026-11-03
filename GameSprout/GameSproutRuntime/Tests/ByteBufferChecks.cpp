#include "Memory/ByteBuffer.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <vector>

using GameSproutRuntime::ByteBuffer;

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool Same(const ByteBuffer& bytes, std::initializer_list<std::uint8_t> expected) {
    return bytes.size() == expected.size() && std::equal(bytes.begin(), bytes.end(), expected.begin());
}
template<typename Operation>
void LengthRejected(Operation operation) {
    bool rejected = false;
    try { operation(); } catch (const std::length_error&) { rejected = true; }
    Check(rejected, "Overflowing byte count was not rejected before allocation");
}
}

int main() {
    static_assert(std::is_nothrow_move_constructible_v<ByteBuffer>);
    static_assert(std::is_nothrow_move_assignable_v<ByteBuffer>);
    static_assert(std::is_nothrow_swappable_v<ByteBuffer>);
    try {
        ByteBuffer empty;
        Check(empty.empty() && empty.size() == 0 && empty.capacity() == 0 && empty.data() == nullptr,
            "Default byte storage was not empty");
        Check(empty.begin() == empty.end() && empty.cbegin() == empty.cend(), "Empty iterators differ");
        empty.assign(nullptr, nullptr);
        empty.resize(0);
        empty.reserve(0);
        ByteBuffer emptyCopy(empty);
        Check(emptyCopy.empty() && emptyCopy.data() == nullptr, "Empty copy allocated storage");

        ByteBuffer bytes{1, 2, 3, 4};
        ByteBuffer copied(bytes);
        Check(Same(copied, {1, 2, 3, 4}) && copied.data() != bytes.data(), "Copy shares writable storage");
        copied[0] = 99;
        Check(bytes[0] == 1, "Copy mutation affected the source");
        copied = bytes;
        Check(Same(copied, {1, 2, 3, 4}), "Copy assignment changed payload bytes");
        copied = copied;
        Check(Same(copied, {1, 2, 3, 4}), "Self-copy lost bytes");

        const auto* sourceAddress = bytes.data();
        ByteBuffer moved(std::move(bytes));
        Check(Same(moved, {1, 2, 3, 4}) && moved.data() == sourceAddress,
            "Move constructor copied instead of transferring storage");
        Check(bytes.empty() && bytes.capacity() == 0 && bytes.data() == nullptr, "Moved-from storage was not reset");
        ByteBuffer destination{8, 9};
        destination = std::move(moved);
        Check(destination.data() == sourceAddress && moved.empty() && moved.data() == nullptr,
            "Move assignment did not release/transfer/reset storage");
        destination = std::move(destination);
        Check(Same(destination, {1, 2, 3, 4}), "Self-move lost bytes");

        destination.reserve(64);
        Check(destination.capacity() >= 64 && Same(destination, {1, 2, 3, 4}), "Reserve changed logical bytes");
        const auto* retained = destination.data();
        destination.reserve(12);
        Check(destination.data() == retained, "Reserve shrank/reallocated sufficient storage");
        destination.resize(7);
        Check(Same(destination, {1, 2, 3, 4, 0, 0, 0}), "Resize expansion was not zero-filled");
        destination[5] = 99;
        destination.resize(3);
        destination.resize(7);
        Check(Same(destination, {1, 2, 3, 0, 0, 0, 0}), "Resize re-exposed removed bytes instead of zeros");
        const auto capacity = destination.capacity();
        destination.clear();
        Check(destination.empty() && destination.data() == retained && destination.capacity() == capacity,
            "Clear released retained storage");
        destination.assign(nullptr, nullptr);
        Check(destination.begin() == destination.end(), "Reserved empty iterators differ");

        destination = {0, 1, 2, 3, 4, 5, 6, 7};
        destination.assign(destination.data() + 2, destination.data() + 7);
        Check(Same(destination, {2, 3, 4, 5, 6}), "Aliased overlapping assignment corrupted its source");
        destination.assign(destination.begin(), destination.end());
        Check(Same(destination, {2, 3, 4, 5, 6}), "Whole self-range assignment changed bytes");
        const ByteBuffer& constant = destination;
        destination.assign(constant.begin() + 1, constant.end());
        Check(Same(destination, {3, 4, 5, 6}), "Const aliased range assignment changed bytes");
        const std::array<std::uint8_t, 3> array{11, 12, 13};
        destination.assign(array.begin(), array.end());
        Check(Same(destination, {11, 12, 13}), "Const array pointer assignment failed");
        std::vector<std::uint8_t> vector{21, 22, 23, 24};
        destination.assign(vector.begin(), vector.end());
        Check(Same(destination, {21, 22, 23, 24}), "Contiguous vector-iterator assignment failed");
        vector.clear();
        destination.assign(vector.begin(), vector.end());
        Check(destination.empty(), "Empty contiguous-iterator assignment failed");

        ByteBuffer grown;
        for (unsigned value = 0; value < 1000; ++value) grown.push_back(static_cast<std::uint8_t>(value));
        Check(grown.size() == 1000 && grown.capacity() >= grown.size(), "Push growth produced invalid extents");
        for (unsigned value = 0; value < 1000; ++value)
            Check(grown[value] == static_cast<std::uint8_t>(value), "Push reallocation lost a previous byte");
        ByteBuffer zeros(12);
        Check(zeros.size() == 12 && std::all_of(zeros.begin(), zeros.end(), [](std::uint8_t value) { return value == 0; }),
            "Size constructor did not zero exposed bytes");
        swap(zeros, grown);
        Check(zeros.size() == 1000 && grown.size() == 12, "Swap did not exchange storage/extents");

        ByteBuffer stable{31, 32, 33};
        const auto* stableAddress = stable.data();
        const auto oversized = ByteBuffer::max_size() + std::size_t{1};
        LengthRejected([&] { stable.reserve(oversized); });
        LengthRejected([&] { stable.resize(oversized); });
        LengthRejected([&] { ByteBuffer excessive(oversized); });
        Check(stable.data() == stableAddress && Same(stable, {31, 32, 33}),
            "Rejected allocation changed the previous byte value");
        bool rangeRejected = false;
        try { stable.assign(array.end(), array.begin()); } catch (const std::invalid_argument&) { rangeRejected = true; }
        Check(rangeRejected && Same(stable, {31, 32, 33}), "Reversed range changed the previous byte value");
        rangeRejected = false;
        try { stable.assign(nullptr, array.data()); } catch (const std::invalid_argument&) { rangeRejected = true; }
        Check(rangeRejected && Same(stable, {31, 32, 33}), "Invalid null range changed the previous byte value");

        std::cout << "PASS ByteBuffer: ownership, copy/move, empty/aliased contiguous ranges, growth, zero-fill and overflow rejection\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << "FAIL " << failure.what() << '\n';
        return 1;
    }
}
