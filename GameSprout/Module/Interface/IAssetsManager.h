#ifndef __IAssetsManager_h__
#define __IAssetsManager_h__

#include "Define.h"
#include "IGuidCreator.h"
#include "IAssetImportSettings.h"
#include "IResourceImportSettings.h"

class IAssetMeta;


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

class IAssetMeta {
public:
    virtual ~IAssetMeta() {}

    IAssetMeta(const eAssetType type, const Guid guid) : _AssetType(type), _Guid(guid) {}

    // 相对于项目 Assets 目录的源资产路径。
    virtual const std::string & GetPath() const = 0;
    virtual IAssetImportSettings * GetImportSettings() const = 0;

    const eAssetType _AssetType;
    const Guid _Guid;
};

enum class eAssetMetaEvent : uint8_t {
    Create,
    Delete,
    Modify
};

class IAssetsManager : public GameSprout::IGameSproutModule {
public:
    virtual ~IAssetsManager() {}

    virtual IAssetMeta * QueryAsset(const Guid &guid) const = 0;
};

#endif //__IAssetsManager_h__
