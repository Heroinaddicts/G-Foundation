#pragma once
#include <JsonCpp/json.hpp>
#include <QByteArray>
#include <QtEndian>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

// BinaryValueLE1: typed structural resource body, not a file header.
// Tags: null/false/true/int64/uint64/float64/UTF8/array/object = 0..8.
// Numbers and uint32 counts/string lengths use little endian. No JSON parsing.
namespace AssetValueBinary {
inline void U32(QByteArray & bytes, uint32_t value) {
    const uint32_t encoded = qToLittleEndian(value);
    bytes.append(reinterpret_cast<const char *>(&encoded), 4);
}
inline void U64(QByteArray & bytes, uint64_t value) {
    const uint64_t encoded = qToLittleEndian(value);
    bytes.append(reinterpret_cast<const char *>(&encoded), 8);
}
inline bool String(QByteArray & bytes, const std::string & value) {
    if (value.size() > std::numeric_limits<uint32_t>::max()) return false;
    U32(bytes, static_cast<uint32_t>(value.size()));
    bytes.append(value.data(), static_cast<qsizetype>(value.size()));
    return true;
}
inline bool Write(QByteArray & bytes, const Json::Value & value, unsigned depth, uint32_t & nodes) {
    if (depth > 64 || ++nodes > 1000000) return false;
    switch (value.type()) {
    case Json::nullValue: bytes.append(char(0));return true;
    case Json::booleanValue: bytes.append(char(value.asBool() ? 2 : 1));return true;
    case Json::intValue: bytes.append(char(3));U64(bytes, std::bit_cast<uint64_t>(int64_t(value.asInt64())));return true;
    case Json::uintValue: bytes.append(char(4));U64(bytes, value.asUInt64());return true;
    case Json::realValue:
        if (!std::isfinite(value.asDouble())) return false;
        bytes.append(char(5));U64(bytes, std::bit_cast<uint64_t>(value.asDouble()));return true;
    case Json::stringValue: bytes.append(char(6));return String(bytes, value.asString());
    case Json::arrayValue:
        bytes.append(char(7));U32(bytes, value.size());
        for (const auto & child : value) if (!Write(bytes, child, depth + 1, nodes)) return false;
        return true;
    case Json::objectValue:
        bytes.append(char(8));U32(bytes, value.size());
        for (const auto & key : value.getMemberNames())
            if (!String(bytes, key) || !Write(bytes, value[key], depth + 1, nodes)) return false;
        return true;
    }
    return false;
}
inline bool Encode(const Json::Value & value, QByteArray & output) {
    QByteArray bytes;
    uint32_t nodes = 0;
    if (!Write(bytes, value, 0, nodes)) return false;
    output = std::move(bytes);
    return true;
}

class Reader {
public:
    explicit Reader(const QByteArray & bytes) : _Bytes(bytes) {}
    bool Read(Json::Value & value, unsigned depth = 0) {
        if (depth > 64 || ++_Nodes > 1000000 || _Offset >= _Bytes.size()) return false;
        const uint8_t tag = static_cast<uint8_t>(_Bytes[_Offset++]);
        uint64_t number;
        uint32_t count;
        std::string text;
        switch (tag) {
        case 0: value = Json::Value();return true;
        case 1: value = false;return true;
        case 2: value = true;return true;
        case 3: if (!Number(number)) return false;value = Json::Int64(std::bit_cast<int64_t>(number));return true;
        case 4: if (!Number(number)) return false;value = Json::UInt64(number);return true;
        case 5:
            if (!Number(number) || !std::isfinite(std::bit_cast<double>(number))) return false;
            value = std::bit_cast<double>(number);return true;
        case 6: if (!Text(text)) return false;value = text;return true;
        case 7:
        case 8:
            if (!Count(count) || count > 1000000 || count > static_cast<uint64_t>(_Bytes.size() - _Offset)) return false;
            value = Json::Value(tag == 7 ? Json::arrayValue : Json::objectValue);
            for (uint32_t i = 0; i < count; ++i) {
                Json::Value child;
                if (tag == 8 && (!Text(text) || value.isMember(text))) return false;
                if (!Read(child, depth + 1)) return false;
                if (tag == 7) value.append(std::move(child));else value[text] = std::move(child);
            }
            return true;
        default: return false;
        }
    }
    bool End() const { return _Offset == _Bytes.size(); }
private:
    bool Count(uint32_t & value) {
        if (_Bytes.size() - _Offset < 4) return false;
        value = qFromLittleEndian<uint32_t>(reinterpret_cast<const uchar *>(_Bytes.constData() + _Offset));
        _Offset += 4;return true;
    }
    bool Number(uint64_t & value) {
        if (_Bytes.size() - _Offset < 8) return false;
        value = qFromLittleEndian<uint64_t>(reinterpret_cast<const uchar *>(_Bytes.constData() + _Offset));
        _Offset += 8;return true;
    }
    bool Text(std::string & value) {
        uint32_t length;
        if (!Count(length) || length > static_cast<uint64_t>(_Bytes.size() - _Offset)) return false;
        value.assign(_Bytes.constData() + _Offset, length);_Offset += length;return true;
    }
    const QByteArray & _Bytes;
    qsizetype _Offset = 0;
    uint32_t _Nodes = 0;
};
inline bool Decode(const QByteArray & bytes, Json::Value & output) {
    Reader reader(bytes);
    Json::Value value;
    if (!reader.Read(value) || !reader.End()) return false;
    output = std::move(value);
    return true;
}
}
