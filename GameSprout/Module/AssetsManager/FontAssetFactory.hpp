#pragma once

#include "AssetFactoryCommon.hpp"
#include "ImportSettingsJson.hpp"

class FontImportSettings : public IFontImportSettings, public IImportSettingConverter {
public:
    virtual ~FontImportSettings() {}

    bool ConvertToJson(Json::Value & value) const override {
        if (!Validate()) {
            return false;
        }
        Json::Value result(Json::objectValue);
        result["FontSize"] = static_cast<Json::UInt>(_FontSize);
        if (!AssetSettingsJson::WriteEnum(result["RenderMode"], _RenderMode, RenderModeNames)) {
            return false;
        }
        result["IncludeFontData"] = _IncludeFontData;
        result["CharacterPadding"] = static_cast<Json::UInt>(_CharacterPadding);
        result["CharacterSpacing"] = static_cast<Json::Int>(_CharacterSpacing);
        result["CustomCharacters"] = _CustomCharacters;
        value.swap(result);
        return true;
    }

    bool ConvertFromJson(const Json::Value & value) override {
        if (!value.isObject()) {
            return false;
        }
        FontImportSettings settings;
        if (!AssetSettingsJson::Read(value, "FontSize", settings._FontSize)
            || !AssetSettingsJson::ReadEnum(value, "RenderMode", settings._RenderMode, RenderModeNames)
            || !AssetSettingsJson::Read(value, "IncludeFontData", settings._IncludeFontData)
            || !AssetSettingsJson::Read(value, "CharacterPadding", settings._CharacterPadding)
            || !AssetSettingsJson::Read(value, "CharacterSpacing", settings._CharacterSpacing)
            || !AssetSettingsJson::Read(value, "CustomCharacters", settings._CustomCharacters)
            || !settings.Validate()) {
            return false;
        }
        *this = settings;
        return true;
    }

    // 生成字形时使用的像素字号，必须大于零。
    uint32_t GetFontSize() const override {
        return _FontSize;
    }

    bool SetFontSize(const uint32_t value) override {
        if (!(value > 0)) {
            return false;
        }
        _FontSize = value;
        return true;
    }

    // 平滑或像素栅格字形。
    eFontRenderMode GetRenderMode() const override {
        return _RenderMode;
    }

    void SetRenderMode(const eFontRenderMode value) override {
        _RenderMode = value;
    }

    // 是否保留原始字体数据以便运行时生成字形。
    bool GetIncludeFontData() const override {
        return _IncludeFontData;
    }

    void SetIncludeFontData(const bool value) override {
        _IncludeFontData = value;
    }

    // 字形边缘留白像素。
    uint32_t GetCharacterPadding() const override {
        return _CharacterPadding;
    }

    void SetCharacterPadding(const uint32_t value) override {
        _CharacterPadding = value;
    }

    // 额外字符间距。
    int32_t GetCharacterSpacing() const override {
        return _CharacterSpacing;
    }

    void SetCharacterSpacing(const int32_t value) override {
        _CharacterSpacing = value;
    }

    // 需要预先生成的 UTF-8 字符集合；空字符串由管线采用默认策略。
    const std::string & GetCustomCharacters() const override {
        return _CustomCharacters;
    }

    void SetCustomCharacters(const std::string & value) override {
        _CustomCharacters = value;
    }

private:
    bool Validate() const {
        return (_FontSize > 0);
    }

    inline static constexpr AssetSettingsJson::EnumName<eFontRenderMode> RenderModeNames[] = {
        {eFontRenderMode::Smooth, "Smooth"},
        {eFontRenderMode::Raster, "Raster"},
    };

    uint32_t _FontSize = 16;
    eFontRenderMode _RenderMode = eFontRenderMode::Smooth;
    bool _IncludeFontData = true;
    uint32_t _CharacterPadding = 1;
    int32_t _CharacterSpacing = 0;
    std::string _CustomCharacters = "";
};

class FontMeta : public AssetMetaWithSettings<FontImportSettings> {
public:
    FontMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetImporterType::Font, guid, path) {}
};

inline IAssetMeta * OnFontAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<FontMeta>(event);
}
