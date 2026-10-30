#ifndef __IAssetsManager_h__
#define __IAssetsManager_h__

#include "Define.h"
#include "IGuidCreator.h"

#include <string>
#include <vector>

class IAssetImportSettings {
public:
    virtual ~IAssetImportSettings() {}
};

class IFolderImportSettings : public IAssetImportSettings {
public:
    virtual ~IFolderImportSettings() {}
};

class ITextureImportSettings : public IAssetImportSettings {
public:
    virtual ~ITextureImportSettings() {}
    // ==================== 颜色 ====================
    // SRGB：普通颜色图片。
    // Linear：法线、粗糙度、遮罩等数值数据。
    virtual eTextureColorSpace GetColorSpace() const = 0;
    virtual void SetColorSpace(const eTextureColorSpace colorSpace) = 0;


    // ==================== 透明通道 ====================
    // 忽略 Alpha，或者使用源图片的 Alpha。
    virtual eTextureAlphaSource GetAlphaSource() const = 0;
    virtual void SetAlphaSource(const eTextureAlphaSource alphaSource) = 0;

    // 是否处理透明区域附近的颜色，减轻过滤产生的黑边、白边。
    // Alpha 用来保存数值数据时，应关闭。
    virtual bool GetAlphaIsTransparency() const = 0;
    virtual void SetAlphaIsTransparency(const bool enabled) = 0;


    // ==================== 尺寸 ====================
    // 导入结果的最大边长，单位：像素。
    // 超出限制时等比例缩小，不主动放大小图片。
    virtual uint32_t GetMaxSize() const = 0;

    // 无效或不支持的尺寸返回 false，并保留原设置。
    virtual bool SetMaxSize(const uint32_t maxSize) = 0;


    // ==================== Mipmap ====================
    // 是否生成逐级缩小的图片版本。
    virtual bool GetGenerateMipmaps() const = 0;
    virtual void SetGenerateMipmaps(const bool enabled) = 0;


    // ==================== CPU 像素副本 ====================
    // 运行时上传 GPU 后，是否继续保留 CPU 可访问的像素数据。
    // 这不控制 GPU 是否可以写入贴图。
    virtual bool GetKeepCPUCopy() const = 0;
    virtual void SetKeepCPUCopy(const bool enabled) = 0;


    // ==================== 默认采样设置 ====================
    // Point：像素边界清晰。
    // Bilinear：平滑邻近像素。
    // Trilinear：同时平滑相邻 Mip 层之间的过渡。
    virtual eTextureFilterMode GetFilterMode() const = 0;
    virtual void SetFilterMode(const eTextureFilterMode filterMode) = 0;

    // 水平方向超出图片边界后的处理方式。
    virtual eTextureWrapMode GetWrapU() const = 0;
    virtual void SetWrapU(const eTextureWrapMode wrapMode) = 0;

    // 垂直方向超出图片边界后的处理方式。
    virtual eTextureWrapMode GetWrapV() const = 0;
    virtual void SetWrapV(const eTextureWrapMode wrapMode) = 0;
};

class IAudioImportSettings : public IAssetImportSettings {
public:
    virtual ~IAudioImportSettings() {}

    // 是否将多声道混合为单声道。
    virtual bool GetForceToMono() const = 0;
    virtual void SetForceToMono(const bool value) = 0;

    // 转为单声道时是否归一化音量。
    virtual bool GetNormalize() const = 0;
    virtual void SetNormalize(const bool value) = 0;

    // 解压后加载、压缩驻留，或流式读取。
    virtual eAudioLoadType GetLoadType() const = 0;
    virtual void SetLoadType(const eAudioLoadType value) = 0;

    // 预处理音频的编码格式，不是源文件后缀。
    virtual eAudioCompressionFormat GetCompressionFormat() const = 0;
    virtual void SetCompressionFormat(const eAudioCompressionFormat value) = 0;

    // 有损编码质量，范围 0 到 1。
    virtual float GetQuality() const = 0;
    virtual bool SetQuality(const float value) = 0;

    // 保持、自动优化，或覆盖采样率。
    virtual eAudioSampleRateMode GetSampleRateMode() const = 0;
    virtual void SetSampleRateMode(const eAudioSampleRateMode value) = 0;

    // 覆盖模式下的采样率，必须大于零。
    virtual uint32_t GetSampleRateOverride() const = 0;
    virtual bool SetSampleRateOverride(const uint32_t value) = 0;

    // 是否请求预加载音频数据。
    virtual bool GetPreloadAudioData() const = 0;
    virtual void SetPreloadAudioData(const bool value) = 0;

    // 是否允许后台加载。
    virtual bool GetLoadInBackground() const = 0;
    virtual void SetLoadInBackground(const bool value) = 0;

    // 源数据是否按照 Ambisonic 环绕声处理。
    virtual bool GetAmbisonic() const = 0;
    virtual void SetAmbisonic(const bool value) = 0;

};

class IMeshImportSettings : public IAssetImportSettings {
public:
    virtual ~IMeshImportSettings() {}

    // 导出数据使用的缩放倍率，必须大于零。
    virtual float GetScaleFactor() const = 0;
    virtual bool SetScaleFactor(const float value) = 0;

    // 导入源法线、重新计算，或不生成法线。
    virtual eMeshNormalsMode GetNormalsMode() const = 0;
    virtual void SetNormalsMode(const eMeshNormalsMode value) = 0;

    // 导入源切线、重新计算，或不生成切线。
    virtual eMeshTangentsMode GetTangentsMode() const = 0;
    virtual void SetTangentsMode(const eMeshTangentsMode value) = 0;

    // 重新计算法线时的平滑角度，范围 0 到 180 度。
    virtual float GetNormalSmoothingAngle() const = 0;
    virtual bool SetNormalSmoothingAngle(const float value) = 0;

    // 网格数据压缩等级，由后续管线实现。
    virtual eMeshCompression GetMeshCompression() const = 0;
    virtual void SetMeshCompression(const eMeshCompression value) = 0;

    // 是否优化顶点和索引的排列。
    virtual bool GetOptimizeMesh() const = 0;
    virtual void SetOptimizeMesh(const bool value) = 0;

    // 是否合并符合条件的重复顶点。
    virtual bool GetWeldVertices() const = 0;
    virtual void SetWeldVertices(const bool value) = 0;

    // 是否生成光照贴图使用的额外 UV。
    virtual bool GetGenerateLightmapUVs() const = 0;
    virtual void SetGenerateLightmapUVs(const bool value) = 0;

    // 运行时是否保留 CPU 可访问的网格数据。
    virtual bool GetKeepCPUCopy() const = 0;
    virtual void SetKeepCPUCopy(const bool value) = 0;

};

class IModelImportSettings : public IMeshImportSettings {
public:
    virtual ~IModelImportSettings() {}

    // 是否使用模型文件声明的单位比例。
    virtual bool GetUseFileScale() const = 0;
    virtual void SetUseFileScale(const bool value) = 0;

    // 是否提取模型中的动画。
    virtual bool GetImportAnimation() const = 0;
    virtual void SetImportAnimation(const bool value) = 0;

    // 不导入骨骼动画、通用骨骼，或人形骨骼。
    virtual eModelAnimationType GetAnimationType() const = 0;
    virtual void SetAnimationType(const eModelAnimationType value) = 0;

    // 是否提取材质。
    virtual bool GetImportMaterials() const = 0;
    virtual void SetImportMaterials(const bool value) = 0;

    // 是否提取形态目标。
    virtual bool GetImportBlendShapes() const = 0;
    virtual void SetImportBlendShapes(const bool value) = 0;

    // 是否提取模型中的摄像机描述。
    virtual bool GetImportCameras() const = 0;
    virtual void SetImportCameras(const bool value) = 0;

    // 是否提取模型中的灯光描述。
    virtual bool GetImportLights() const = 0;
    virtual void SetImportLights(const bool value) = 0;

};

class IShaderImportSettings : public IAssetImportSettings {
public:
    virtual ~IShaderImportSettings() {}
    virtual eShaderStage GetStage() const = 0;
    virtual void SetStage(eShaderStage stage) = 0;

    // 当前只支持 HLSL 源码。
    virtual eShaderSourceLanguage GetSourceLanguage() const = 0;
    virtual void SetSourceLanguage(const eShaderSourceLanguage value) = 0;

    // 编译入口名称；实际有效性由 Shader 管线检查。
    virtual const std::string & GetEntryPoint() const = 0;
    virtual void SetEntryPoint(const std::string & value) = 0;

    // 编译使用的宏定义，例如 USE_FOG=1。
    virtual const std::vector<std::string> & GetDefines() const = 0;
    virtual void SetDefines(const std::vector<std::string> & value) = 0;

    // 是否请求编译器优化输出。
    virtual bool GetOptimize() const = 0;
    virtual void SetOptimize(const bool value) = 0;

    // 默认生成 SPIR-V/MSL；启用 DXIL 还需要可用的 DXC downstream 编译器。
    virtual bool GetGenerateDXIL() const = 0;
    virtual void SetGenerateDXIL(const bool value) = 0;

    // 是否在编译产物中保留调试信息。
    virtual bool GetGenerateDebugInfo() const = 0;
    virtual void SetGenerateDebugInfo(const bool value) = 0;

};

class IComputeShaderImportSettings : public IAssetImportSettings {
public:
    virtual ~IComputeShaderImportSettings() {}

    // 当前只支持 HLSL 源码。
    virtual eShaderSourceLanguage GetSourceLanguage() const = 0;
    virtual void SetSourceLanguage(const eShaderSourceLanguage value) = 0;

    // 编译入口名称；实际有效性由 Shader 管线检查。
    virtual const std::string & GetEntryPoint() const = 0;
    virtual void SetEntryPoint(const std::string & value) = 0;

    // 编译使用的宏定义，例如 USE_FOG=1。
    virtual const std::vector<std::string> & GetDefines() const = 0;
    virtual void SetDefines(const std::vector<std::string> & value) = 0;

    // 是否请求编译器优化输出。
    virtual bool GetOptimize() const = 0;
    virtual void SetOptimize(const bool value) = 0;

    // 默认生成 SPIR-V/MSL；启用 DXIL 还需要可用的 DXC downstream 编译器。
    virtual bool GetGenerateDXIL() const = 0;
    virtual void SetGenerateDXIL(const bool value) = 0;

    // 是否在编译产物中保留调试信息。
    virtual bool GetGenerateDebugInfo() const = 0;
    virtual void SetGenerateDebugInfo(const bool value) = 0;

};

class IFontImportSettings : public IAssetImportSettings {
public:
    virtual ~IFontImportSettings() {}

    // 生成字形时使用的像素字号，必须大于零。
    virtual uint32_t GetFontSize() const = 0;
    virtual bool SetFontSize(const uint32_t value) = 0;

    // 平滑或像素栅格字形。
    virtual eFontRenderMode GetRenderMode() const = 0;
    virtual void SetRenderMode(const eFontRenderMode value) = 0;

    // 是否保留原始字体数据以便运行时生成字形。
    virtual bool GetIncludeFontData() const = 0;
    virtual void SetIncludeFontData(const bool value) = 0;

    // 字形边缘留白像素。
    virtual uint32_t GetCharacterPadding() const = 0;
    virtual void SetCharacterPadding(const uint32_t value) = 0;

    // 额外字符间距。
    virtual int32_t GetCharacterSpacing() const = 0;
    virtual void SetCharacterSpacing(const int32_t value) = 0;

    // 需要预先生成的 UTF-8 字符集合；空字符串由管线采用默认策略。
    virtual const std::string & GetCustomCharacters() const = 0;
    virtual void SetCustomCharacters(const std::string & value) = 0;

};

class IVideoImportSettings : public IAssetImportSettings {
public:
    virtual ~IVideoImportSettings() {}

    // 是否请求视频转码。
    virtual bool GetTranscode() const = 0;
    virtual void SetTranscode(const bool value) = 0;

    // 转码编码器；设备兼容性由目标管线检查。
    virtual eVideoCodec GetCodec() const = 0;
    virtual void SetCodec(const eVideoCodec value) = 0;

    // 转码结果的最大宽度，必须大于零。
    virtual uint32_t GetMaxWidth() const = 0;
    virtual bool SetMaxWidth(const uint32_t value) = 0;

    // 转码结果的最大高度，必须大于零。
    virtual uint32_t GetMaxHeight() const = 0;
    virtual bool SetMaxHeight(const uint32_t value) = 0;

    // 是否保留音轨。
    virtual bool GetImportAudio() const = 0;
    virtual void SetImportAudio(const bool value) = 0;

    // 是否请求保留透明通道。
    virtual bool GetEncodeAlpha() const = 0;
    virtual void SetEncodeAlpha(const bool value) = 0;

    // 是否水平翻转画面。
    virtual bool GetFlipHorizontal() const = 0;
    virtual void SetFlipHorizontal(const bool value) = 0;

    // 是否垂直翻转画面。
    virtual bool GetFlipVertical() const = 0;
    virtual void SetFlipVertical(const bool value) = 0;

};

class ITextImportSettings : public IAssetImportSettings {
public:
    virtual ~ITextImportSettings() {}

    // 文本编码；Binary 表示按原始字节处理。
    virtual eTextEncoding GetEncoding() const = 0;
    virtual void SetEncoding(const eTextEncoding value) = 0;

    // 是否将文本换行统一为 LF；Binary 模式不应用文本处理。
    virtual bool GetNormalizeLineEndings() const = 0;
    virtual void SetNormalizeLineEndings(const bool value) = 0;

    // 解码文本时是否移除 BOM。
    virtual bool GetStripByteOrderMark() const = 0;
    virtual void SetStripByteOrderMark(const bool value) = 0;

};

class IScriptImportSettings : public IAssetImportSettings {
public:
    virtual ~IScriptImportSettings() {}

    // 脚本默认执行顺序。
    virtual int32_t GetExecutionOrder() const = 0;
    virtual void SetExecutionOrder(const int32_t value) = 0;

    // 是否请求生成脚本调试信息；不代表已经接入编译器。
    virtual bool GetGenerateDebugInfo() const = 0;
    virtual void SetGenerateDebugInfo(const bool value) = 0;

};

// 已经序列化的资源使用通用配置；材质/动画/场景自身属性保存在源文件中。
class INativeAssetImportSettings : public IAssetImportSettings {
public:
    virtual ~INativeAssetImportSettings() {}
    // 0 表示由管线选择主对象；其他值标识文件内的主资源。
    virtual uint64_t GetMainObjectID() const = 0;
    virtual void SetMainObjectID(const uint64_t value) = 0;
};

class IMaterialImportSettings : public INativeAssetImportSettings {
public:
    virtual ~IMaterialImportSettings() {}
};

class IAnimationClipImportSettings : public INativeAssetImportSettings {
public:
    virtual ~IAnimationClipImportSettings() {}
};

class IAnimatorControllerImportSettings : public INativeAssetImportSettings {
public:
    virtual ~IAnimatorControllerImportSettings() {}
};

class IAnimatorOverrideControllerImportSettings : public INativeAssetImportSettings {
public:
    virtual ~IAnimatorOverrideControllerImportSettings() {}
};

class IAvatarMaskImportSettings : public INativeAssetImportSettings {
public:
    virtual ~IAvatarMaskImportSettings() {}
};

class IAudioMixerImportSettings : public INativeAssetImportSettings {
public:
    virtual ~IAudioMixerImportSettings() {}
};

class ISceneImportSettings : public INativeAssetImportSettings {
public:
    virtual ~ISceneImportSettings() {}
};

class IPhysicsMaterialImportSettings : public INativeAssetImportSettings {
public:
    virtual ~IPhysicsMaterialImportSettings() {}
};

class IPhysicsMaterial2DImportSettings : public INativeAssetImportSettings {
public:
    virtual ~IPhysicsMaterial2DImportSettings() {}
};

class IRenderTextureImportSettings : public INativeAssetImportSettings {
public:
    virtual ~IRenderTextureImportSettings() {}
};

class ICubemapImportSettings : public INativeAssetImportSettings {
public:
    virtual ~ICubemapImportSettings() {}
};

class ISpriteAtlasImportSettings : public INativeAssetImportSettings {
public:
    virtual ~ISpriteAtlasImportSettings() {}
};

class ITerrainDataImportSettings : public INativeAssetImportSettings {
public:
    virtual ~ITerrainDataImportSettings() {}
};

class ITerrainLayerImportSettings : public INativeAssetImportSettings {
public:
    virtual ~ITerrainLayerImportSettings() {}
};

class IShaderVariantCollectionImportSettings : public INativeAssetImportSettings {
public:
    virtual ~IShaderVariantCollectionImportSettings() {}
};

class IDataAssetImportSettings : public INativeAssetImportSettings {
public:
    virtual ~IDataAssetImportSettings() {}
};

class ILightingSettingsImportSettings : public INativeAssetImportSettings {
public:
    virtual ~ILightingSettingsImportSettings() {}
};

class IShaderIncludeImportSettings : public IAssetImportSettings {
public:
    virtual ~IShaderIncludeImportSettings() {}
};

class ILightmapParametersImportSettings : public INativeAssetImportSettings {
public:
    virtual ~ILightmapParametersImportSettings() {}
};

class IAsset;

enum class eAssetImportState : uint8_t { Pending, Ready, Failed, NotRequired };

class IAssetMeta {
public:
    virtual ~IAssetMeta() {}

    IAssetMeta(const eAssetImporterType type, const Guid guid) : _ImporterType(type), _Guid(guid) {}

    // 相对于项目 Assets 目录的源资产路径。
    virtual const std::string & GetPath() const = 0;
    // 借用指针：Meta 重载或成功应用新设置后，必须重新获取。
    virtual IAssetImportSettings * GetImportSettings() const = 0;
    // 主线程查询；仅返回最近成功导入的有效子资源，排除主资源和历史身份记录。
    // 借用指针，重新导入/删除后失效。UI 长期保存 GUID + LocalID。
    virtual std::vector<IAsset *> GetSubAssets() const = 0;
    virtual eAssetImportState GetImportState() const = 0;
    virtual std::string GetImportError() const = 0;

    // UTF-8 JSON snapshot; workers never retain this Meta pointer.
    virtual bool SerializeMeta(std::string & json) const = 0;
    virtual bool SetSubAssets(const std::string & json) = 0;
    virtual bool SaveMeta() = 0;

    const eAssetImporterType _ImporterType;
    const Guid _Guid;
};

enum class eAssetMetaEvent : uint8_t { Create, Modify, Remove };

// 删除后 QueryAsset 可能返回 nullptr，因此事件携带独立快照，不借用 Meta。
struct AssetEvent {
    AssetEvent(eAssetMetaEvent action, const Guid & guid, std::string path,
        std::string oldPath, eAssetImporterType importer, eAssetImportState state, std::string error)
        : Action(action), GuidValue(guid), Path(std::move(path)), OldPath(std::move(oldPath)),
          Importer(importer), State(state), Error(std::move(error)) {}
    eAssetMetaEvent Action;
    const Guid GuidValue;
    std::string Path;
    std::string OldPath;
    eAssetImporterType Importer;
    eAssetImportState State;
    std::string Error;
};

class IAssetListener {
public:
    virtual ~IAssetListener() {}
    virtual void OnAssetCreate(const AssetEvent & event) = 0;
    virtual void OnAssetModify(const AssetEvent & event) = 0;
    virtual void OnAssetRemove(const AssetEvent & event) = 0;
};

class IAssetsManager : public GameSprout::IGameSproutModule {
public:
    virtual ~IAssetsManager() {}

    virtual IAssetMeta * QueryAsset(const Guid &guid) const = 0;
    virtual bool ReimportAsset(const Guid & guid) = 0;
    // 主线程：校验并保存设置后安排导入。expected 是编辑开始时的设置快照，
    // 外部修改造成冲突时返回 false；验证/保存失败不替换原设置。
    virtual bool ApplyImportSettings(const Guid & guid, const std::string & settingsJson,
        const std::string & expectedSettingsJson, std::string & error) = 0;
    // Borrowed until source removal or module release; main thread only.
    virtual std::vector<IAssetMeta *> GetAssetMetas() const = 0;
    // path 相对 Assets；空字符串表示根目录。只返回直接子项，目录在前。
    virtual std::vector<IAssetMeta *> GetAssetsInDirectory(const std::string & path) const = 0;
    // 全部在主线程调用；不持有监听者，释放监听者前必须注销。
    // 通知在主线程 Update 派发，事件仅在回调期间借用。
    // 先订阅，再查询初始快照；通知不回放历史，查询反映派发时的最新状态。
    // Modify 包含改名、导入状态和子资源变化；仅改名/移动时 OldPath 非空。
    virtual bool RegisterAssetListener(IAssetListener * listener) = 0;
    virtual void UnregisterAssetListener(IAssetListener * listener) = 0;
};

#endif //__IAssetsManager_h__
