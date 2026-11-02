#pragma once

#include "AssetFactoryCommon.hpp"
#include "ImportSettingsJson.hpp"
#include <QRegularExpression>

class ShaderImportSettings : public IShaderImportSettings, public IImportSettingConverter {
public:
    virtual ~ShaderImportSettings() {}

    bool ConvertToJson(Json::Value & value) const override {
        if (!Validate()) {
            return false;
        }
        Json::Value result(Json::objectValue);
        if (!AssetSettingsJson::WriteEnum(result["SourceLanguage"], _SourceLanguage, SourceLanguageNames)) {
            return false;
        }
        result["Defines"] = Json::Value(Json::arrayValue);
        for (const std::string & item : _Defines) {
            result["Defines"].append(item);
        }
        result["GenerateDXIL"] = _GenerateDXIL;
        result["Optimize"] = _Optimize;
        result["GenerateDebugInfo"] = _GenerateDebugInfo;
        value.swap(result);
        return true;
    }

    bool ConvertFromJson(const Json::Value & value) override {
        if (!value.isObject()) {
            return false;
        }
        ShaderImportSettings settings;
        if (!AssetSettingsJson::ReadEnum(value, "SourceLanguage", settings._SourceLanguage, SourceLanguageNames)
            || !AssetSettingsJson::Read(value, "Defines", settings._Defines)
            || !AssetSettingsJson::Read(value, "GenerateDXIL", settings._GenerateDXIL)
            || !AssetSettingsJson::Read(value, "Optimize", settings._Optimize)
            || !AssetSettingsJson::Read(value, "GenerateDebugInfo", settings._GenerateDebugInfo)
            || !settings.Validate()) {
            return false;
        }
        *this = settings;
        return true;
    }

    // 当前只支持 HLSL 源码。
    eShaderSourceLanguage GetSourceLanguage() const override {
        return _SourceLanguage;
    }

    void SetSourceLanguage(const eShaderSourceLanguage value) override {
        _SourceLanguage = value;
    }

    // 编译使用的宏定义，例如 USE_FOG=1。
    const std::vector<std::string> & GetDefines() const override {
        return _Defines;
    }

    void SetDefines(const std::vector<std::string> & value) override {
        _Defines = value;
    }

    bool GetGenerateDXIL() const override { return _GenerateDXIL; }
    void SetGenerateDXIL(const bool value) override { _GenerateDXIL = value; }

    // 是否请求编译器优化输出。
    bool GetOptimize() const override {
        return _Optimize;
    }

    void SetOptimize(const bool value) override {
        _Optimize = value;
    }

    // 是否在编译产物中保留调试信息。
    bool GetGenerateDebugInfo() const override {
        return _GenerateDebugInfo;
    }

    void SetGenerateDebugInfo(const bool value) override {
        _GenerateDebugInfo = value;
    }

private:
    bool Validate() const {
        if (_SourceLanguage != eShaderSourceLanguage::HLSL) return false;
        std::set<std::string> names;
        for (const auto & define : _Defines) {
            const QString value=QString::fromStdString(define), name=value.section('=',0,0);
            if (!QRegularExpression("^[A-Za-z_][A-Za-z0-9_]*$").match(name).hasMatch() || value.contains('\n') || value.contains('\r') || value.contains(QChar('\0')) || !names.insert(name.toStdString()).second) return false;
        }
        return true;
    }

    inline static constexpr AssetSettingsJson::EnumName<eShaderSourceLanguage> SourceLanguageNames[] = {
        {eShaderSourceLanguage::HLSL, "HLSL"},
    };

    eShaderSourceLanguage _SourceLanguage = eShaderSourceLanguage::HLSL;
    std::vector<std::string> _Defines = {};
    bool _GenerateDXIL = false;
    bool _Optimize = true;
    bool _GenerateDebugInfo = false;
};

class ShaderMeta : public AssetMetaWithSettings<ShaderImportSettings> {
public:
    ShaderMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetImporterType::Shader, guid, path) {

    }

};

inline IAssetMeta * OnShaderAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<ShaderMeta>(event);
}
