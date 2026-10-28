#pragma once

#include "AssetFactoryCommon.hpp"
#include "ImportSettingsJson.hpp"

class TextImportSettings : public ITextImportSettings, public IImportSettingConverter {
public:
    virtual ~TextImportSettings() {}

    bool ConvertToJson(Json::Value & value) const override {
        if (!Validate()) {
            return false;
        }
        Json::Value result(Json::objectValue);
        if (!AssetSettingsJson::WriteEnum(result["Encoding"], _Encoding, EncodingNames)) {
            return false;
        }
        result["NormalizeLineEndings"] = _NormalizeLineEndings;
        result["StripByteOrderMark"] = _StripByteOrderMark;
        value.swap(result);
        return true;
    }

    bool ConvertFromJson(const Json::Value & value) override {
        if (!value.isObject()) {
            return false;
        }
        TextImportSettings settings;
        if (!AssetSettingsJson::ReadEnum(value, "Encoding", settings._Encoding, EncodingNames)
            || !AssetSettingsJson::Read(value, "NormalizeLineEndings", settings._NormalizeLineEndings)
            || !AssetSettingsJson::Read(value, "StripByteOrderMark", settings._StripByteOrderMark)
            || !settings.Validate()) {
            return false;
        }
        *this = settings;
        return true;
    }

    // 文本编码；Binary 表示按原始字节处理。
    eTextEncoding GetEncoding() const override {
        return _Encoding;
    }

    void SetEncoding(const eTextEncoding value) override {
        _Encoding = value;
    }

    // 是否将文本换行统一为 LF；Binary 模式不应用文本处理。
    bool GetNormalizeLineEndings() const override {
        return _NormalizeLineEndings;
    }

    void SetNormalizeLineEndings(const bool value) override {
        _NormalizeLineEndings = value;
    }

    // 解码文本时是否移除 BOM。
    bool GetStripByteOrderMark() const override {
        return _StripByteOrderMark;
    }

    void SetStripByteOrderMark(const bool value) override {
        _StripByteOrderMark = value;
    }

private:
    bool Validate() const {
        return true;
    }

    inline static constexpr AssetSettingsJson::EnumName<eTextEncoding> EncodingNames[] = {
        {eTextEncoding::Auto, "Auto"},
        {eTextEncoding::UTF8, "UTF8"},
        {eTextEncoding::UTF16LE, "UTF16LE"},
        {eTextEncoding::UTF16BE, "UTF16BE"},
        {eTextEncoding::Binary, "Binary"},
    };

    eTextEncoding _Encoding = eTextEncoding::Auto;
    bool _NormalizeLineEndings = false;
    bool _StripByteOrderMark = true;
};

class TextMeta : public AssetMetaWithSettings<TextImportSettings> {
public:
    TextMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetType::Text, guid, path) {}
};

inline IAssetMeta * OnTextAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<TextMeta>(event);
}
