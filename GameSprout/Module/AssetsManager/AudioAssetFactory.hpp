#pragma once

#include "AssetFactoryCommon.hpp"
#include "ImportSettingsJson.hpp"

class AudioImportSettings : public IAudioImportSettings, public IImportSettingConverter {
public:
    virtual ~AudioImportSettings() {}

    bool ConvertToJson(Json::Value & value) const override {
        if (!Validate()) {
            return false;
        }
        Json::Value result(Json::objectValue);
        result["ForceToMono"] = _ForceToMono;
        result["Normalize"] = _Normalize;
        if (!AssetSettingsJson::WriteEnum(result["LoadType"], _LoadType, LoadTypeNames)) {
            return false;
        }
        if (!AssetSettingsJson::WriteEnum(result["CompressionFormat"], _CompressionFormat, CompressionFormatNames)) {
            return false;
        }
        result["Quality"] = _Quality;
        if (!AssetSettingsJson::WriteEnum(result["SampleRateMode"], _SampleRateMode, SampleRateModeNames)) {
            return false;
        }
        result["SampleRateOverride"] = static_cast<Json::UInt>(_SampleRateOverride);
        result["PreloadAudioData"] = _PreloadAudioData;
        result["LoadInBackground"] = _LoadInBackground;
        result["Ambisonic"] = _Ambisonic;
        value.swap(result);
        return true;
    }

    bool ConvertFromJson(const Json::Value & value) override {
        if (!value.isObject()) {
            return false;
        }
        AudioImportSettings settings;
        if (!AssetSettingsJson::Read(value, "ForceToMono", settings._ForceToMono)
            || !AssetSettingsJson::Read(value, "Normalize", settings._Normalize)
            || !AssetSettingsJson::ReadEnum(value, "LoadType", settings._LoadType, LoadTypeNames)
            || !AssetSettingsJson::ReadEnum(value, "CompressionFormat", settings._CompressionFormat, CompressionFormatNames)
            || !AssetSettingsJson::Read(value, "Quality", settings._Quality)
            || !AssetSettingsJson::ReadEnum(value, "SampleRateMode", settings._SampleRateMode, SampleRateModeNames)
            || !AssetSettingsJson::Read(value, "SampleRateOverride", settings._SampleRateOverride)
            || !AssetSettingsJson::Read(value, "PreloadAudioData", settings._PreloadAudioData)
            || !AssetSettingsJson::Read(value, "LoadInBackground", settings._LoadInBackground)
            || !AssetSettingsJson::Read(value, "Ambisonic", settings._Ambisonic)
            || !settings.Validate()) {
            return false;
        }
        *this = settings;
        return true;
    }

    // 是否将多声道混合为单声道。
    bool GetForceToMono() const override {
        return _ForceToMono;
    }

    void SetForceToMono(const bool value) override {
        _ForceToMono = value;
    }

    // 转为单声道时是否归一化音量。
    bool GetNormalize() const override {
        return _Normalize;
    }

    void SetNormalize(const bool value) override {
        _Normalize = value;
    }

    // 解压后加载、压缩驻留，或流式读取。
    eAudioLoadType GetLoadType() const override {
        return _LoadType;
    }

    void SetLoadType(const eAudioLoadType value) override {
        _LoadType = value;
    }

    // 预处理音频的编码格式，不是源文件后缀。
    eAudioCompressionFormat GetCompressionFormat() const override {
        return _CompressionFormat;
    }

    void SetCompressionFormat(const eAudioCompressionFormat value) override {
        _CompressionFormat = value;
    }

    // 有损编码质量，范围 0 到 1。
    float GetQuality() const override {
        return _Quality;
    }

    bool SetQuality(const float value) override {
        if (!(std::isfinite(value) && value >= 0.0f && value <= 1.0f)) {
            return false;
        }
        _Quality = value;
        return true;
    }

    // 保持、自动优化，或覆盖采样率。
    eAudioSampleRateMode GetSampleRateMode() const override {
        return _SampleRateMode;
    }

    void SetSampleRateMode(const eAudioSampleRateMode value) override {
        _SampleRateMode = value;
    }

    // 覆盖模式下的采样率，必须大于零。
    uint32_t GetSampleRateOverride() const override {
        return _SampleRateOverride;
    }

    bool SetSampleRateOverride(const uint32_t value) override {
        if (!(value > 0)) {
            return false;
        }
        _SampleRateOverride = value;
        return true;
    }

    // 是否请求预加载音频数据。
    bool GetPreloadAudioData() const override {
        return _PreloadAudioData;
    }

    void SetPreloadAudioData(const bool value) override {
        _PreloadAudioData = value;
    }

    // 是否允许后台加载。
    bool GetLoadInBackground() const override {
        return _LoadInBackground;
    }

    void SetLoadInBackground(const bool value) override {
        _LoadInBackground = value;
    }

    // 源数据是否按照 Ambisonic 环绕声处理。
    bool GetAmbisonic() const override {
        return _Ambisonic;
    }

    void SetAmbisonic(const bool value) override {
        _Ambisonic = value;
    }

private:
    bool Validate() const {
        return (std::isfinite(_Quality) && _Quality >= 0.0f && _Quality <= 1.0f)
            && (_SampleRateOverride > 0);
    }

    inline static constexpr AssetSettingsJson::EnumName<eAudioLoadType> LoadTypeNames[] = {
        {eAudioLoadType::DecompressOnLoad, "DecompressOnLoad"},
        {eAudioLoadType::CompressedInMemory, "CompressedInMemory"},
        {eAudioLoadType::Streaming, "Streaming"},
    };

    inline static constexpr AssetSettingsJson::EnumName<eAudioCompressionFormat> CompressionFormatNames[] = {
        {eAudioCompressionFormat::PCM, "PCM"},
        {eAudioCompressionFormat::Vorbis, "Vorbis"},
        {eAudioCompressionFormat::ADPCM, "ADPCM"},
    };

    inline static constexpr AssetSettingsJson::EnumName<eAudioSampleRateMode> SampleRateModeNames[] = {
        {eAudioSampleRateMode::Preserve, "Preserve"},
        {eAudioSampleRateMode::Optimize, "Optimize"},
        {eAudioSampleRateMode::Override, "Override"},
    };

    bool _ForceToMono = false;
    bool _Normalize = true;
    eAudioLoadType _LoadType = eAudioLoadType::DecompressOnLoad;
    eAudioCompressionFormat _CompressionFormat = eAudioCompressionFormat::Vorbis;
    float _Quality = 1.0f;
    eAudioSampleRateMode _SampleRateMode = eAudioSampleRateMode::Preserve;
    uint32_t _SampleRateOverride = 44100;
    bool _PreloadAudioData = true;
    bool _LoadInBackground = false;
    bool _Ambisonic = false;
};

class AudioMeta : public AssetMetaWithSettings<AudioImportSettings> {
public:
    AudioMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetImporterType::Audio, guid, path) {}
};

inline IAssetMeta * OnAudioAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<AudioMeta>(event);
}
