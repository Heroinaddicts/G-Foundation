#pragma once
#include "JsonCpp/json.hpp"
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// Current BinaryValueLE1 structural encoding, shared by prepared Prefabs and
// their authoring validation. Counts and numeric payloads are little endian.
namespace ObjectDocumentBinary {
inline void Number(std::vector<std::uint8_t>& bytes,std::uint64_t value,unsigned count) {
    for(unsigned i=0;i<count;++i)bytes.push_back(static_cast<std::uint8_t>(value>>(i*8)));
}
inline bool Text(std::vector<std::uint8_t>& bytes,const std::string& value) {
    if(value.size()>std::numeric_limits<std::uint32_t>::max())return false;
    Number(bytes,value.size(),4);bytes.insert(bytes.end(),value.begin(),value.end());return true;
}
inline bool Write(std::vector<std::uint8_t>& bytes,const Json::Value& value,unsigned depth,std::uint32_t& nodes) {
    if(depth>64||++nodes>1000000)return false;
    switch(value.type()) {
    case Json::nullValue:bytes.push_back(0);return true;
    case Json::booleanValue:bytes.push_back(value.asBool()?2:1);return true;
    case Json::intValue:bytes.push_back(3);Number(bytes,std::bit_cast<std::uint64_t>(std::int64_t(value.asInt64())),8);return true;
    case Json::uintValue:bytes.push_back(4);Number(bytes,value.asUInt64(),8);return true;
    case Json::realValue:
        if(!std::isfinite(value.asDouble()))return false;
        bytes.push_back(5);Number(bytes,std::bit_cast<std::uint64_t>(value.asDouble()),8);return true;
    case Json::stringValue:bytes.push_back(6);return Text(bytes,value.asString());
    case Json::arrayValue:
        bytes.push_back(7);Number(bytes,value.size(),4);
        for(const auto& child:value)if(!Write(bytes,child,depth+1,nodes))return false;
        return true;
    case Json::objectValue:
        bytes.push_back(8);Number(bytes,value.size(),4);
        for(const auto& key:value.getMemberNames())if(!Text(bytes,key)||!Write(bytes,value[key],depth+1,nodes))return false;
        return true;
    }
    return false;
}
inline bool Encode(const Json::Value& value,std::vector<std::uint8_t>& output) {
    std::vector<std::uint8_t> bytes;std::uint32_t nodes=0;
    if(!Write(bytes,value,0,nodes))return false;
    output.swap(bytes);return true;
}
class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes):_Bytes(bytes){}
    bool Read(Json::Value& value,unsigned depth=0) {
        if(depth>64||++_Nodes>1000000||_Offset>=_Bytes.size())return false;
        const auto tag=_Bytes[_Offset++];std::uint64_t number;std::string text;
        switch(tag) {
        case 0:value=Json::Value();return true;
        case 1:value=false;return true;
        case 2:value=true;return true;
        case 3:if(!Number(number,8))return false;value=Json::Int64(std::bit_cast<std::int64_t>(number));return true;
        case 4:if(!Number(number,8))return false;value=Json::UInt64(number);return true;
        case 5:if(!Number(number,8)||!std::isfinite(std::bit_cast<double>(number)))return false;value=std::bit_cast<double>(number);return true;
        case 6:if(!Text(text))return false;value=text;return true;
        case 7:case 8:
            if(!Number(number,4)||number>1000000||number>_Bytes.size()-_Offset)return false;
            value=Json::Value(tag==7?Json::arrayValue:Json::objectValue);
            for(std::uint64_t i=0;i<number;++i) {
                Json::Value child;
                if(tag==8&&(!Text(text)||value.isMember(text)))return false;
                if(!Read(child,depth+1))return false;
                if(tag==7)value.append(std::move(child));else value[text]=std::move(child);
            }
            return true;
        default:return false;
        }
    }
    bool End()const{return _Offset==_Bytes.size();}
private:
    bool Number(std::uint64_t& value,unsigned count) {
        if(_Bytes.size()-_Offset<count)return false;
        value=0;for(unsigned i=0;i<count;++i)value|=std::uint64_t(_Bytes[_Offset++])<<(i*8);
        return true;
    }
    bool Text(std::string& value) {
        std::uint64_t count;
        if(!Number(count,4)||count>_Bytes.size()-_Offset)return false;
        value.assign(reinterpret_cast<const char*>(_Bytes.data()+_Offset),static_cast<std::size_t>(count));
        _Offset+=static_cast<std::size_t>(count);return true;
    }
    const std::vector<std::uint8_t>& _Bytes;
    std::size_t _Offset=0;
    std::uint32_t _Nodes=0;
};
inline bool Decode(const std::vector<std::uint8_t>& bytes,Json::Value& output) {
    Reader reader(bytes);Json::Value value;
    if(!reader.Read(value)||!reader.End())return false;
    output.swap(value);return true;
}
}
