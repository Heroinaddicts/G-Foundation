#pragma once

#include "AssetFactoryCommon.hpp"
#include "ImportSettingsJson.hpp"
#include "../Shared/AnimationImportSettingsJson.h"
#include "../Shared/HumanoidDefinition.h"

class ModelImportSettings : public IModelImportSettings, public IImportSettingConverter {
public:
    virtual ~ModelImportSettings() {}

    bool ConvertToJson(Json::Value & value) const override {
        if (!Validate()) {
            return false;
        }
        Json::Value result(Json::objectValue);
        result["ScaleFactor"] = _ScaleFactor;
        if (!AssetSettingsJson::WriteEnum(result["NormalsMode"], _NormalsMode, NormalsModeNames)) {
            return false;
        }
        if (!AssetSettingsJson::WriteEnum(result["TangentsMode"], _TangentsMode, TangentsModeNames)) {
            return false;
        }
        result["NormalSmoothingAngle"] = _NormalSmoothingAngle;
        if (!AssetSettingsJson::WriteEnum(result["MeshCompression"], _MeshCompression, MeshCompressionNames)) {
            return false;
        }
        result["OptimizeMesh"] = _OptimizeMesh;
        result["WeldVertices"] = _WeldVertices;
        result["GenerateLightmapUVs"] = _GenerateLightmapUVs;
        result["KeepCPUCopy"] = _KeepCPUCopy;
        result["UseFileScale"] = _UseFileScale;
        result["ImportAnimation"] = _ImportAnimation;
        result["AnimationClips"]=Json::Value(Json::arrayValue);
        for(const auto& clip:_AnimationClips)result["AnimationClips"].append(AnimationImportSettingsJson::Write(clip,true));
        if (!AssetSettingsJson::WriteEnum(result["AnimationType"], _AnimationType, AnimationTypeNames)) {
            return false;
        }
        const auto human=HumanoidDefinition::SettingsJson(_HumanoidSettings);
        for(const auto& name:human.getMemberNames())result[name]=human[name];
        result["ImportMaterials"] = _ImportMaterials;
        result["ImportBlendShapes"] = _ImportBlendShapes;
        result["ImportCameras"] = _ImportCameras;
        result["ImportLights"] = _ImportLights;
        result["MaterialRemaps"] = _MaterialRemaps;
        value.swap(result);
        return true;
    }

    bool ConvertFromJson(const Json::Value & value) override {
        if (!value.isObject()) {
            return false;
        }
        ModelImportSettings settings;
        std::string humanError;
        if(!HumanoidDefinition::ReadSettings(value,settings._HumanoidSettings,humanError))return false;
        const Json::Value remaps = value.get("MaterialRemaps", Json::Value(Json::objectValue));
        if (!remaps.isObject()) return false;
        for (const std::string & key : remaps.getMemberNames()) {
            const auto & reference = remaps[key];
            if (key.empty() || !reference.isObject() || !reference["Guid"].isString()
                || !Guid(reference["Guid"].asString()).IsValid() || !reference["LocalID"].isUInt64()
                || reference["LocalID"].asUInt64() == 0) return false;
        }
        settings._MaterialRemaps = remaps;
        if(value.isMember("AnimationClips")&&!AnimationImportSettingsJson::ReadModel(value["AnimationClips"],settings._AnimationClips))return false;
        if (!AssetSettingsJson::Read(value, "ScaleFactor", settings._ScaleFactor)
            || !AssetSettingsJson::ReadEnum(value, "NormalsMode", settings._NormalsMode, NormalsModeNames)
            || !AssetSettingsJson::ReadEnum(value, "TangentsMode", settings._TangentsMode, TangentsModeNames)
            || !AssetSettingsJson::Read(value, "NormalSmoothingAngle", settings._NormalSmoothingAngle)
            || !AssetSettingsJson::ReadEnum(value, "MeshCompression", settings._MeshCompression, MeshCompressionNames)
            || !AssetSettingsJson::Read(value, "OptimizeMesh", settings._OptimizeMesh)
            || !AssetSettingsJson::Read(value, "WeldVertices", settings._WeldVertices)
            || !AssetSettingsJson::Read(value, "GenerateLightmapUVs", settings._GenerateLightmapUVs)
            || !AssetSettingsJson::Read(value, "KeepCPUCopy", settings._KeepCPUCopy)
            || !AssetSettingsJson::Read(value, "UseFileScale", settings._UseFileScale)
            || !AssetSettingsJson::Read(value, "ImportAnimation", settings._ImportAnimation)
            || !AssetSettingsJson::ReadEnum(value, "AnimationType", settings._AnimationType, AnimationTypeNames)
            || !AssetSettingsJson::Read(value, "ImportMaterials", settings._ImportMaterials)
            || !AssetSettingsJson::Read(value, "ImportBlendShapes", settings._ImportBlendShapes)
            || !AssetSettingsJson::Read(value, "ImportCameras", settings._ImportCameras)
            || !AssetSettingsJson::Read(value, "ImportLights", settings._ImportLights)
            || !settings.Validate()) {
            return false;
        }
        *this = settings;
        return true;
    }

    const std::vector<AnimationClipImportOptions>& GetAnimationClips() const override {return _AnimationClips;}
    bool SetAnimationClips(const std::vector<AnimationClipImportOptions>& clips) override {
        Json::Value value(Json::arrayValue);for(const auto& clip:clips){if(!AnimationImportSettingsJson::Validate(clip,true))return false;value.append(AnimationImportSettingsJson::Write(clip,true));}
        std::vector<AnimationClipImportOptions> checked;if(!AnimationImportSettingsJson::ReadModel(value,checked))return false;_AnimationClips=std::move(checked);return true;
    }

    const ModelHumanoidImportSettings& GetHumanoidSettings() const override {return _HumanoidSettings;}
    bool SetHumanoidSettings(const ModelHumanoidImportSettings& settings) override {
        ModelHumanoidImportSettings checked;std::string error;
        if(!HumanoidDefinition::ReadSettings(HumanoidDefinition::SettingsJson(settings),checked,error))return false;
        _HumanoidSettings=std::move(checked);return true;
    }

    // 导出数据使用的缩放倍率，必须大于零。
    float GetScaleFactor() const override {
        return _ScaleFactor;
    }

    bool SetScaleFactor(const float value) override {
        if (!(std::isfinite(value) && value > 0.0f)) {
            return false;
        }
        _ScaleFactor = value;
        return true;
    }

    // 导入源法线、重新计算，或不生成法线。
    eMeshNormalsMode GetNormalsMode() const override {
        return _NormalsMode;
    }

    void SetNormalsMode(const eMeshNormalsMode value) override {
        _NormalsMode = value;
    }

    // 导入源切线、重新计算，或不生成切线。
    eMeshTangentsMode GetTangentsMode() const override {
        return _TangentsMode;
    }

    void SetTangentsMode(const eMeshTangentsMode value) override {
        _TangentsMode = value;
    }

    // 重新计算法线时的平滑角度，范围 0 到 180 度。
    float GetNormalSmoothingAngle() const override {
        return _NormalSmoothingAngle;
    }

    bool SetNormalSmoothingAngle(const float value) override {
        if (!(std::isfinite(value) && value >= 0.0f && value <= 180.0f)) {
            return false;
        }
        _NormalSmoothingAngle = value;
        return true;
    }

    // 网格数据压缩等级，由后续管线实现。
    eMeshCompression GetMeshCompression() const override {
        return _MeshCompression;
    }

    void SetMeshCompression(const eMeshCompression value) override {
        _MeshCompression = value;
    }

    // 是否优化顶点和索引的排列。
    bool GetOptimizeMesh() const override {
        return _OptimizeMesh;
    }

    void SetOptimizeMesh(const bool value) override {
        _OptimizeMesh = value;
    }

    // 是否合并符合条件的重复顶点。
    bool GetWeldVertices() const override {
        return _WeldVertices;
    }

    void SetWeldVertices(const bool value) override {
        _WeldVertices = value;
    }

    // 是否生成光照贴图使用的额外 UV。
    bool GetGenerateLightmapUVs() const override {
        return _GenerateLightmapUVs;
    }

    void SetGenerateLightmapUVs(const bool value) override {
        _GenerateLightmapUVs = value;
    }

    // 运行时是否保留 CPU 可访问的网格数据。
    bool GetKeepCPUCopy() const override {
        return _KeepCPUCopy;
    }

    void SetKeepCPUCopy(const bool value) override {
        _KeepCPUCopy = value;
    }

    // 是否使用模型文件声明的单位比例。
    bool GetUseFileScale() const override {
        return _UseFileScale;
    }

    void SetUseFileScale(const bool value) override {
        _UseFileScale = value;
    }

    // 是否提取模型中的动画。
    bool GetImportAnimation() const override {
        return _ImportAnimation;
    }

    void SetImportAnimation(const bool value) override {
        _ImportAnimation = value;
    }

    // 不导入骨骼动画、通用骨骼，或人形骨骼。
    eModelAnimationType GetAnimationType() const override {
        return _AnimationType;
    }

    void SetAnimationType(const eModelAnimationType value) override {
        _AnimationType = value;
    }

    // 是否提取材质。
    bool GetImportMaterials() const override {
        return _ImportMaterials;
    }

    void SetImportMaterials(const bool value) override {
        _ImportMaterials = value;
    }

    // 是否提取形态目标。
    bool GetImportBlendShapes() const override {
        return _ImportBlendShapes;
    }

    void SetImportBlendShapes(const bool value) override {
        _ImportBlendShapes = value;
    }

    // 是否提取模型中的摄像机描述。
    bool GetImportCameras() const override {
        return _ImportCameras;
    }

    void SetImportCameras(const bool value) override {
        _ImportCameras = value;
    }

    // 是否提取模型中的灯光描述。
    bool GetImportLights() const override {
        return _ImportLights;
    }

    void SetImportLights(const bool value) override {
        _ImportLights = value;
    }

private:
    bool Validate() const {
        return (std::isfinite(_ScaleFactor) && _ScaleFactor > 0.0f)
            && (std::isfinite(_NormalSmoothingAngle) && _NormalSmoothingAngle >= 0.0f && _NormalSmoothingAngle <= 180.0f);
    }

    inline static constexpr AssetSettingsJson::EnumName<eMeshNormalsMode> NormalsModeNames[] = {
        {eMeshNormalsMode::Import, "Import"},
        {eMeshNormalsMode::Calculate, "Calculate"},
        {eMeshNormalsMode::None, "None"},
    };

    inline static constexpr AssetSettingsJson::EnumName<eMeshTangentsMode> TangentsModeNames[] = {
        {eMeshTangentsMode::Import, "Import"},
        {eMeshTangentsMode::Calculate, "Calculate"},
        {eMeshTangentsMode::None, "None"},
    };

    inline static constexpr AssetSettingsJson::EnumName<eMeshCompression> MeshCompressionNames[] = {
        {eMeshCompression::Off, "Off"},
        {eMeshCompression::Low, "Low"},
        {eMeshCompression::Medium, "Medium"},
        {eMeshCompression::High, "High"},
    };

    inline static constexpr AssetSettingsJson::EnumName<eModelAnimationType> AnimationTypeNames[] = {
        {eModelAnimationType::None, "None"},
        {eModelAnimationType::Generic, "Generic"},
        {eModelAnimationType::Humanoid, "Humanoid"},
    };

    float _ScaleFactor = 1.0f;
    eMeshNormalsMode _NormalsMode = eMeshNormalsMode::Import;
    eMeshTangentsMode _TangentsMode = eMeshTangentsMode::Calculate;
    float _NormalSmoothingAngle = 60.0f;
    eMeshCompression _MeshCompression = eMeshCompression::Off;
    bool _OptimizeMesh = true;
    bool _WeldVertices = true;
    bool _GenerateLightmapUVs = false;
    bool _KeepCPUCopy = false;
    bool _UseFileScale = true;
    bool _ImportAnimation = true;
    std::vector<AnimationClipImportOptions> _AnimationClips;
    eModelAnimationType _AnimationType = eModelAnimationType::Generic;
    ModelHumanoidImportSettings _HumanoidSettings;
    bool _ImportMaterials = true;
    bool _ImportBlendShapes = true;
    bool _ImportCameras = false;
    bool _ImportLights = false;
    Json::Value _MaterialRemaps{Json::objectValue};
};

class ModelMeta : public AssetMetaWithSettings<ModelImportSettings> {
public:
    ModelMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetImporterType::Model, guid, path) {}
};

inline IAssetMeta * OnModelAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<ModelMeta>(event);
}
