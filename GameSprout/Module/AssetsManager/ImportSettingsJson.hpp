#pragma once

#include "Header.h"
#include <cmath>
#include <limits>
#include <vector>

// 导入设置共用的严格 JSON 读取规则；缺失字段保留传入的默认值。
namespace AssetSettingsJson {
    inline bool Read(const Json::Value & object, const char * name, bool & result) {
        if (!object.isMember(name)) return true;
        const Json::Value & value = object[name];
        if (!value.isBool()) return false;
        result = value.asBool();
        return true;
    }

    inline bool Read(const Json::Value & object, const char * name, uint32_t & result) {
        if (!object.isMember(name)) return true;
        const Json::Value & value = object[name];
        if ((value.type() != Json::intValue && value.type() != Json::uintValue) || !value.isUInt()) return false;
        result = value.asUInt();
        return true;
    }

    inline bool Read(const Json::Value & object, const char * name, uint64_t & result) {
        if (!object.isMember(name)) return true;
        const Json::Value & value = object[name];
        if ((value.type() != Json::intValue && value.type() != Json::uintValue) || !value.isUInt64()) return false;
        result = value.asUInt64();
        return true;
    }

    inline bool Read(const Json::Value & object, const char * name, int32_t & result) {
        if (!object.isMember(name)) return true;
        const Json::Value & value = object[name];
        if ((value.type() != Json::intValue && value.type() != Json::uintValue) || !value.isInt()) return false;
        result = value.asInt();
        return true;
    }

    inline bool Read(const Json::Value & object, const char * name, float & result) {
        if (!object.isMember(name)) return true;
        const Json::Value & value = object[name];
        if (!value.isNumeric()) return false;
        const double number = value.asDouble();
        if (!std::isfinite(number) || number > std::numeric_limits<float>::max()
            || number < -std::numeric_limits<float>::max()) return false;
        result = static_cast<float>(number);
        return true;
    }

    inline bool Read(const Json::Value & object, const char * name, std::string & result) {
        if (!object.isMember(name)) return true;
        const Json::Value & value = object[name];
        if (!value.isString()) return false;
        result = value.asString();
        return true;
    }

    inline bool Read(const Json::Value & object, const char * name, std::vector<std::string> & result) {
        if (!object.isMember(name)) return true;
        const Json::Value & value = object[name];
        if (!value.isArray()) return false;
        std::vector<std::string> items;
        items.reserve(value.size());
        for (const Json::Value & item : value) {
            if (!item.isString()) return false;
            items.push_back(item.asString());
        }
        result.swap(items);
        return true;
    }

    template<typename Enum>
    struct EnumName {
        Enum Value;
        const char * Name;
    };

    template<typename Enum, size_t Count>
    bool ReadEnum(const Json::Value & object, const char * name, Enum & result,
                  const EnumName<Enum> (&names)[Count]) {
        if (!object.isMember(name)) return true;
        const Json::Value & value = object[name];
        if (!value.isString()) return false;
        const std::string text = value.asString();
        for (const EnumName<Enum> & entry : names) {
            if (text == entry.Name) {
                result = entry.Value;
                return true;
            }
        }
        return false;
    }

    template<typename Enum, size_t Count>
    bool WriteEnum(Json::Value & value, const Enum input, const EnumName<Enum> (&names)[Count]) {
        for (const EnumName<Enum> & entry : names) {
            if (input == entry.Value) {
                value = entry.Name;
                return true;
            }
        }
        return false;
    }
}
