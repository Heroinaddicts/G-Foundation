#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace GameSproutRuntime {

// Owning contiguous bytes for large resource payloads. Bulk copies use memcpy
// and releasing storage does not call an allocator hook once per byte in Debug.
// This is a byte container, not a general object container or shared owner.
class ByteBuffer final {
public:
    using value_type = std::uint8_t;
    using size_type = std::size_t;
    using iterator = value_type*;
    using const_iterator = const value_type*;

    ByteBuffer() noexcept = default;
    explicit ByteBuffer(size_type count) { resize(count); }
    ByteBuffer(std::initializer_list<value_type> values) { assign(values); }
    ByteBuffer(const ByteBuffer& other) { AssignBytes(other._Data, other._Size); }
    ByteBuffer(ByteBuffer&& other) noexcept { swap(other); }
    ~ByteBuffer() { delete[] _Data; }

    ByteBuffer& operator=(const ByteBuffer& other) {
        if (this != &other) {
            ByteBuffer candidate(other);
            swap(candidate);
        }
        return *this;
    }
    ByteBuffer& operator=(ByteBuffer&& other) noexcept {
        if (this != &other) {
            delete[] _Data;
            _Data = other._Data;
            _Size = other._Size;
            _Capacity = other._Capacity;
            other._Data = nullptr;
            other._Size = other._Capacity = 0;
        }
        return *this;
    }
    ByteBuffer& operator=(std::initializer_list<value_type> values) {
        assign(values);
        return *this;
    }

    static constexpr size_type max_size() noexcept {
        return static_cast<size_type>((std::numeric_limits<std::ptrdiff_t>::max)());
    }
    size_type size() const noexcept { return _Size; }
    size_type capacity() const noexcept { return _Capacity; }
    bool empty() const noexcept { return _Size == 0; }
    value_type* data() noexcept { return _Data; }
    const value_type* data() const noexcept { return _Data; }
    iterator begin() noexcept { return _Data; }
    const_iterator begin() const noexcept { return _Data; }
    const_iterator cbegin() const noexcept { return _Data; }
    iterator end() noexcept { return _Size ? _Data + _Size : _Data; }
    const_iterator end() const noexcept { return _Size ? _Data + _Size : _Data; }
    const_iterator cend() const noexcept { return _Size ? _Data + _Size : _Data; }
    value_type& operator[](size_type index) noexcept { return _Data[index]; }
    const value_type& operator[](size_type index) const noexcept { return _Data[index]; }

    void clear() noexcept { _Size = 0; }
    void swap(ByteBuffer& other) noexcept {
        std::swap(_Data, other._Data);
        std::swap(_Size, other._Size);
        std::swap(_Capacity, other._Capacity);
    }
    void reserve(size_type count) {
        CheckSize(count);
        if (count <= _Capacity) return;
        // No value-initialization here: only the active prefix contains bytes.
        value_type* replacement = new value_type[count];
        if (_Size) std::memcpy(replacement, _Data, _Size);
        delete[] _Data;
        _Data = replacement;
        _Capacity = count;
    }
    void resize(size_type count) {
        CheckSize(count);
        if (count > _Capacity) reserve(Growth(count));
        // Like vector<uint8_t>, expanding exposes zero-valued new bytes.
        if (count > _Size) std::memset(_Data + _Size, 0, count - _Size);
        _Size = count;
    }
    void push_back(value_type value) {
        if (_Size == max_size()) throw std::length_error("ByteBuffer size exceeds its addressable range");
        if (_Size == _Capacity) reserve(Growth(_Size + 1));
        _Data[_Size++] = value;
    }

    // A nonempty pointer range must refer to one valid contiguous byte array.
    // Empty ranges, including (nullptr, nullptr), never subtract/dereference null.
    void assign(const value_type* first, const value_type* last) {
        if (first == last) { clear(); return; }
        if (!first || !last || std::less<const value_type*>{}(last, first))
            throw std::invalid_argument("ByteBuffer requires a valid forward byte range");
        AssignBytes(first, static_cast<size_type>(last - first));
    }
    template<std::contiguous_iterator Iterator>
        requires std::same_as<std::remove_cv_t<std::iter_value_t<Iterator>>, value_type>
    void assign(Iterator first, Iterator last) {
        if (first == last) { clear(); return; }
        const auto count = last - first;
        if (count < 0) throw std::invalid_argument("ByteBuffer requires a forward byte range");
        AssignBytes(std::to_address(first), static_cast<size_type>(count));
    }
    void assign(std::initializer_list<value_type> values) {
        AssignBytes(values.begin(), values.size());
    }

private:
    static void CheckSize(size_type count) {
        if (count > max_size()) throw std::length_error("ByteBuffer size exceeds its addressable range");
    }
    size_type Growth(size_type required) const noexcept {
        const size_type doubled = _Capacity > max_size() / 2 ? max_size() : _Capacity * 2;
        return doubled > required ? doubled : required;
    }
    void AssignBytes(const value_type* source, size_type count) {
        CheckSize(count);
        if (!count) { clear(); return; }
        if (!source) throw std::invalid_argument("ByteBuffer requires nonnull source bytes");
        if (count <= _Capacity) {
            // Supports assigning a subrange of our own active storage.
            std::memmove(_Data, source, count);
            _Size = count;
            return;
        }
        // Copy before releasing the previous allocation. Allocation failure
        // leaves this value unchanged, and an aliased source stays alive.
        value_type* replacement = new value_type[count];
        std::memcpy(replacement, source, count);
        delete[] _Data;
        _Data = replacement;
        _Size = _Capacity = count;
    }
    value_type* _Data = nullptr;
    size_type _Size = 0;
    size_type _Capacity = 0;
};

inline void swap(ByteBuffer& left, ByteBuffer& right) noexcept { left.swap(right); }

}
