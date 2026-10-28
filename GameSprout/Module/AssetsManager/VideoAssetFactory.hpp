#pragma once

#include "AssetFactoryCommon.hpp"
#include "ImportSettingsJson.hpp"

class VideoImportSettings : public IVideoImportSettings, public IImportSettingConverter {
public:
    virtual ~VideoImportSettings() {}

    bool ConvertToJson(Json::Value & value) const override {
        if (!Validate()) {
            return false;
        }
        Json::Value result(Json::objectValue);
        result["Transcode"] = _Transcode;
        if (!AssetSettingsJson::WriteEnum(result["Codec"], _Codec, CodecNames)) {
            return false;
        }
        result["MaxWidth"] = static_cast<Json::UInt>(_MaxWidth);
        result["MaxHeight"] = static_cast<Json::UInt>(_MaxHeight);
        result["ImportAudio"] = _ImportAudio;
        result["EncodeAlpha"] = _EncodeAlpha;
        result["FlipHorizontal"] = _FlipHorizontal;
        result["FlipVertical"] = _FlipVertical;
        value.swap(result);
        return true;
    }

    bool ConvertFromJson(const Json::Value & value) override {
        if (!value.isObject()) {
            return false;
        }
        VideoImportSettings settings;
        if (!AssetSettingsJson::Read(value, "Transcode", settings._Transcode)
            || !AssetSettingsJson::ReadEnum(value, "Codec", settings._Codec, CodecNames)
            || !AssetSettingsJson::Read(value, "MaxWidth", settings._MaxWidth)
            || !AssetSettingsJson::Read(value, "MaxHeight", settings._MaxHeight)
            || !AssetSettingsJson::Read(value, "ImportAudio", settings._ImportAudio)
            || !AssetSettingsJson::Read(value, "EncodeAlpha", settings._EncodeAlpha)
            || !AssetSettingsJson::Read(value, "FlipHorizontal", settings._FlipHorizontal)
            || !AssetSettingsJson::Read(value, "FlipVertical", settings._FlipVertical)
            || !settings.Validate()) {
            return false;
        }
        *this = settings;
        return true;
    }

    // 是否请求视频转码。
    bool GetTranscode() const override {
        return _Transcode;
    }

    void SetTranscode(const bool value) override {
        _Transcode = value;
    }

    // 转码编码器；设备兼容性由目标管线检查。
    eVideoCodec GetCodec() const override {
        return _Codec;
    }

    void SetCodec(const eVideoCodec value) override {
        _Codec = value;
    }

    // 转码结果的最大宽度，必须大于零。
    uint32_t GetMaxWidth() const override {
        return _MaxWidth;
    }

    bool SetMaxWidth(const uint32_t value) override {
        if (!(value > 0)) {
            return false;
        }
        _MaxWidth = value;
        return true;
    }

    // 转码结果的最大高度，必须大于零。
    uint32_t GetMaxHeight() const override {
        return _MaxHeight;
    }

    bool SetMaxHeight(const uint32_t value) override {
        if (!(value > 0)) {
            return false;
        }
        _MaxHeight = value;
        return true;
    }

    // 是否保留音轨。
    bool GetImportAudio() const override {
        return _ImportAudio;
    }

    void SetImportAudio(const bool value) override {
        _ImportAudio = value;
    }

    // 是否请求保留透明通道。
    bool GetEncodeAlpha() const override {
        return _EncodeAlpha;
    }

    void SetEncodeAlpha(const bool value) override {
        _EncodeAlpha = value;
    }

    // 是否水平翻转画面。
    bool GetFlipHorizontal() const override {
        return _FlipHorizontal;
    }

    void SetFlipHorizontal(const bool value) override {
        _FlipHorizontal = value;
    }

    // 是否垂直翻转画面。
    bool GetFlipVertical() const override {
        return _FlipVertical;
    }

    void SetFlipVertical(const bool value) override {
        _FlipVertical = value;
    }

private:
    bool Validate() const {
        return (_MaxWidth > 0)
            && (_MaxHeight > 0);
    }

    inline static constexpr AssetSettingsJson::EnumName<eVideoCodec> CodecNames[] = {
        {eVideoCodec::Auto, "Auto"},
        {eVideoCodec::H264, "H264"},
        {eVideoCodec::H265, "H265"},
        {eVideoCodec::VP8, "VP8"},
    };

    bool _Transcode = false;
    eVideoCodec _Codec = eVideoCodec::Auto;
    uint32_t _MaxWidth = 1920;
    uint32_t _MaxHeight = 1080;
    bool _ImportAudio = true;
    bool _EncodeAlpha = false;
    bool _FlipHorizontal = false;
    bool _FlipVertical = false;
};

class VideoMeta : public AssetMetaWithSettings<VideoImportSettings> {
public:
    VideoMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetType::Video, guid, path) {}
};

inline IAssetMeta * OnVideoAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<VideoMeta>(event);
}
