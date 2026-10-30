#ifndef __TextureImportFactory_h__
#define __TextureImportFactory_h__

#include "AssetFactoryCommon.hpp"

class TextureImportSettings : public ITextureImportSettings, public IImportSettingConverter {
public:
    virtual ~TextureImportSettings() {}

    TextureImportSettings() = default;

    bool ConvertToJson(Json::Value & value) const override {
        Json::Value result(Json::objectValue);

        switch (_ColorSpace) {
        case eTextureColorSpace::SRGB:
            result["ColorSpace"] = "SRGB";
            break;
        case eTextureColorSpace::Linear:
            result["ColorSpace"] = "Linear";
            break;
        default:
            return false;
        }

        switch (_AlphaSource) {
        case eTextureAlphaSource::None:
            result["AlphaSource"] = "None";
            break;
        case eTextureAlphaSource::FromSource:
            result["AlphaSource"] = "FromSource";
            break;
        default:
            return false;
        }

        switch (_FilterMode) {
        case eTextureFilterMode::Point:
            result["FilterMode"] = "Point";
            break;
        case eTextureFilterMode::Bilinear:
            result["FilterMode"] = "Bilinear";
            break;
        case eTextureFilterMode::Trilinear:
            result["FilterMode"] = "Trilinear";
            break;
        default:
            return false;
        }

        const char * wrapU = WrapModeName(_WrapU);
        const char * wrapV = WrapModeName(_WrapV);
        if (nullptr == wrapU || nullptr == wrapV || 0 == _MaxSize) {
            return false;
        }

        result["WrapU"] = wrapU;
        result["WrapV"] = wrapV;
        result["AlphaIsTransparency"] = _AlphaIsTransparency;
        result["MaxSize"] = static_cast<Json::UInt>(_MaxSize);
        result["GenerateMipmaps"] = _GenerateMipmaps;
        result["KeepCPUCopy"] = _KeepCPUCopy;

        // 所有字段有效后才替换调用者提供的 JSON 对象。
        value.swap(result);
        return true;
    }

    bool ConvertFromJson(const Json::Value & value) override {
        if (!value.isObject()) {
            return false;
        }

        // 完整加载配置：缺失字段使用默认值，失败不修改当前对象。
        TextureImportSettings settings;

        if (value.isMember("ColorSpace")) {
            const Json::Value & field = value["ColorSpace"];
            if (!field.isString()) {
                return false;
            }

            const std::string name = field.asString();
            if ("SRGB" == name) {
                settings._ColorSpace = eTextureColorSpace::SRGB;
            } else if ("Linear" == name) {
                settings._ColorSpace = eTextureColorSpace::Linear;
            } else {
                return false;
            }
        }

        if (value.isMember("AlphaSource")) {
            const Json::Value & field = value["AlphaSource"];
            if (!field.isString()) {
                return false;
            }

            const std::string name = field.asString();
            if ("None" == name) {
                settings._AlphaSource = eTextureAlphaSource::None;
            } else if ("FromSource" == name) {
                settings._AlphaSource = eTextureAlphaSource::FromSource;
            } else {
                return false;
            }
        }

        if (value.isMember("FilterMode")) {
            const Json::Value & field = value["FilterMode"];
            if (!field.isString()) {
                return false;
            }

            const std::string name = field.asString();
            if ("Point" == name) {
                settings._FilterMode = eTextureFilterMode::Point;
            } else if ("Bilinear" == name) {
                settings._FilterMode = eTextureFilterMode::Bilinear;
            } else if ("Trilinear" == name) {
                settings._FilterMode = eTextureFilterMode::Trilinear;
            } else {
                return false;
            }
        }

        if (value.isMember("MaxSize")) {
            const Json::Value & field = value["MaxSize"];
            // 不接受浮点数、字符串、负数或超出 uint32_t 范围的值。
            if ((Json::intValue != field.type() && Json::uintValue != field.type())
                || !field.isUInt() || !settings.SetMaxSize(field.asUInt())) {
                return false;
            }
        }

        if (!ReadBool(value, "AlphaIsTransparency", settings._AlphaIsTransparency)
            || !ReadBool(value, "GenerateMipmaps", settings._GenerateMipmaps)
            || !ReadBool(value, "KeepCPUCopy", settings._KeepCPUCopy)
            || !ReadWrapMode(value, "WrapU", settings._WrapU)
            || !ReadWrapMode(value, "WrapV", settings._WrapV)) {
            return false;
        }

        *this = settings;
        return true;
    }

    eTextureColorSpace GetColorSpace() const override {
        return _ColorSpace;
    }

    void SetColorSpace(const eTextureColorSpace colorSpace) override {
        _ColorSpace = colorSpace;
    }

    eTextureAlphaSource GetAlphaSource() const override {
        return _AlphaSource;
    }

    void SetAlphaSource(const eTextureAlphaSource alphaSource) override {
        _AlphaSource = alphaSource;
    }

    bool GetAlphaIsTransparency() const override {
        return _AlphaIsTransparency;
    }

    void SetAlphaIsTransparency(const bool enabled) override {
        _AlphaIsTransparency = enabled;
    }

    uint32_t GetMaxSize() const override {
        return _MaxSize;
    }

    bool SetMaxSize(const uint32_t maxSize) override {
        if (0 == maxSize) {
            return false;
        }

        // 具体目标平台的尺寸限制，由管线生成输出方案时检查。
        _MaxSize = maxSize;
        return true;
    }

    bool GetGenerateMipmaps() const override {
        return _GenerateMipmaps;
    }

    void SetGenerateMipmaps(const bool enabled) override {
        _GenerateMipmaps = enabled;
    }

    bool GetKeepCPUCopy() const override {
        return _KeepCPUCopy;
    }

    void SetKeepCPUCopy(const bool enabled) override {
        _KeepCPUCopy = enabled;
    }

    eTextureFilterMode GetFilterMode() const override {
        return _FilterMode;
    }

    void SetFilterMode(const eTextureFilterMode filterMode) override {
        _FilterMode = filterMode;
    }

    eTextureWrapMode GetWrapU() const override {
        return _WrapU;
    }

    void SetWrapU(const eTextureWrapMode wrapMode) override {
        _WrapU = wrapMode;
    }

    eTextureWrapMode GetWrapV() const override {
        return _WrapV;
    }

    void SetWrapV(const eTextureWrapMode wrapMode) override {
        _WrapV = wrapMode;
    }

private:
    static bool ReadBool(const Json::Value & value, const char * name, bool & result) {
        if (!value.isMember(name)) {
            return true;
        }

        const Json::Value & field = value[name];
        if (!field.isBool()) {
            return false;
        }

        result = field.asBool();
        return true;
    }

    static const char * WrapModeName(const eTextureWrapMode wrapMode) {
        switch (wrapMode) {
        case eTextureWrapMode::Repeat:
            return "Repeat";
        case eTextureWrapMode::ClampToEdge:
            return "ClampToEdge";
        case eTextureWrapMode::MirrorRepeat:
            return "MirrorRepeat";
        default:
            return nullptr;
        }
    }

    static bool ReadWrapMode(const Json::Value & value, const char * name,
                             eTextureWrapMode & result) {
        if (!value.isMember(name)) {
            return true;
        }

        const Json::Value & field = value[name];
        if (!field.isString()) {
            return false;
        }

        const std::string mode = field.asString();
        if ("Repeat" == mode) {
            result = eTextureWrapMode::Repeat;
        } else if ("ClampToEdge" == mode) {
            result = eTextureWrapMode::ClampToEdge;
        } else if ("MirrorRepeat" == mode) {
            result = eTextureWrapMode::MirrorRepeat;
        } else {
            return false;
        }

        return true;
    }

    eTextureColorSpace _ColorSpace = eTextureColorSpace::SRGB;
    eTextureAlphaSource _AlphaSource = eTextureAlphaSource::FromSource;
    bool _AlphaIsTransparency = false;

    uint32_t _MaxSize = 2048;
    bool _GenerateMipmaps = true;
    bool _KeepCPUCopy = false;

    eTextureFilterMode _FilterMode = eTextureFilterMode::Bilinear;
    eTextureWrapMode _WrapU = eTextureWrapMode::Repeat;
    eTextureWrapMode _WrapV = eTextureWrapMode::Repeat;
};

class TextureMeta : public AssetMetaWithSettings<TextureImportSettings> {
public:
    TextureMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetImporterType::Texture, guid, path) {}
};

inline IAssetMeta * OnTextureAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<TextureMeta>(event);
}

#endif //__TextureImportFactory_h__
