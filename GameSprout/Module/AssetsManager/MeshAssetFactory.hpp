#pragma once

#include "AssetFactoryCommon.hpp"
#include "ImportSettingsJson.hpp"

class MeshImportSettings : public IMeshImportSettings, public IImportSettingConverter {
public:
    virtual ~MeshImportSettings() {}

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
        value.swap(result);
        return true;
    }

    bool ConvertFromJson(const Json::Value & value) override {
        if (!value.isObject()) {
            return false;
        }
        MeshImportSettings settings;
        if (!AssetSettingsJson::Read(value, "ScaleFactor", settings._ScaleFactor)
            || !AssetSettingsJson::ReadEnum(value, "NormalsMode", settings._NormalsMode, NormalsModeNames)
            || !AssetSettingsJson::ReadEnum(value, "TangentsMode", settings._TangentsMode, TangentsModeNames)
            || !AssetSettingsJson::Read(value, "NormalSmoothingAngle", settings._NormalSmoothingAngle)
            || !AssetSettingsJson::ReadEnum(value, "MeshCompression", settings._MeshCompression, MeshCompressionNames)
            || !AssetSettingsJson::Read(value, "OptimizeMesh", settings._OptimizeMesh)
            || !AssetSettingsJson::Read(value, "WeldVertices", settings._WeldVertices)
            || !AssetSettingsJson::Read(value, "GenerateLightmapUVs", settings._GenerateLightmapUVs)
            || !AssetSettingsJson::Read(value, "KeepCPUCopy", settings._KeepCPUCopy)
            || !settings.Validate()) {
            return false;
        }
        *this = settings;
        return true;
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

    float _ScaleFactor = 1.0f;
    eMeshNormalsMode _NormalsMode = eMeshNormalsMode::Import;
    eMeshTangentsMode _TangentsMode = eMeshTangentsMode::Calculate;
    float _NormalSmoothingAngle = 60.0f;
    eMeshCompression _MeshCompression = eMeshCompression::Off;
    bool _OptimizeMesh = true;
    bool _WeldVertices = true;
    bool _GenerateLightmapUVs = false;
    bool _KeepCPUCopy = false;
};

class MeshMeta : public AssetMetaWithSettings<MeshImportSettings> {
public:
    MeshMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetImporterType::Mesh, guid, path) {}
};

inline IAssetMeta * OnMeshAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<MeshMeta>(event);
}
