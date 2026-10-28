#pragma once

#include "AssetFactoryCommon.hpp"
#include "ImportSettingsJson.hpp"

class ScriptImportSettings : public IScriptImportSettings, public IImportSettingConverter {
public:
    virtual ~ScriptImportSettings() {}

    bool ConvertToJson(Json::Value & value) const override {
        if (!Validate()) {
            return false;
        }
        Json::Value result(Json::objectValue);
        result["ExecutionOrder"] = static_cast<Json::Int>(_ExecutionOrder);
        result["GenerateDebugInfo"] = _GenerateDebugInfo;
        value.swap(result);
        return true;
    }

    bool ConvertFromJson(const Json::Value & value) override {
        if (!value.isObject()) {
            return false;
        }
        ScriptImportSettings settings;
        if (!AssetSettingsJson::Read(value, "ExecutionOrder", settings._ExecutionOrder)
            || !AssetSettingsJson::Read(value, "GenerateDebugInfo", settings._GenerateDebugInfo)
            || !settings.Validate()) {
            return false;
        }
        *this = settings;
        return true;
    }

    // 脚本默认执行顺序。
    int32_t GetExecutionOrder() const override {
        return _ExecutionOrder;
    }

    void SetExecutionOrder(const int32_t value) override {
        _ExecutionOrder = value;
    }

    // 是否请求生成脚本调试信息；不代表已经接入编译器。
    bool GetGenerateDebugInfo() const override {
        return _GenerateDebugInfo;
    }

    void SetGenerateDebugInfo(const bool value) override {
        _GenerateDebugInfo = value;
    }

private:
    bool Validate() const {
        return true;
    }

    int32_t _ExecutionOrder = 0;
    bool _GenerateDebugInfo = true;
};

class ScriptMeta : public AssetMetaWithSettings<ScriptImportSettings> {
public:
    ScriptMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetType::Script, guid, path) {}
};

inline IAssetMeta * OnScriptAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<ScriptMeta>(event);
}
