#ifndef __IAssetsManager_h__
#define __IAssetsManager_h__

#include "Define.h"

class IGuid;
class IAsset;
class IAssetMeta;

class IAssetImporter {
public:
    virtual ~IAssetImporter() {}
    IAssetImporter(const eAssetImportType type, IAsset * const asset) : _ImportType(type), _Asset(asset) {}

    virtual IAsset * GetAsset() const = 0;

    typedef std::function<void(const bool, const std::string &, IAssetImporter * const)> fAssetImportCallback;
    virtual void Import(const fAssetImportCallback callback) = 0;

    const eAssetImportType _ImportType;
    IAsset * const _Asset;
};

class IAssetImportSettings {
public:
    virtual ~IAssetImportSettings() {}

    IAssetImportSettings(const eAssetImportType type) : _ImportType(type) {}

    virtual IAssetImporter * GetImporter() const = 0;

    const eAssetImportType _ImportType;
};

class ITextureImportSettings : public IAssetImportSettings {
public:
    virtual ~ITextureImportSettings() {}

    ITextureImportSettings(const eAssetImportType type) : IAssetImportSettings(type) {}
};

class ITextureImporter : public IAssetImporter {
public:
    virtual ~ITextureImporter() {}
    ITextureImporter(const eAssetImportType type,  IAsset * const asset, ITextureImportSettings * const settings) : IAssetImporter(type, asset), _Settings(settings) {}

    ITextureImportSettings * const _Settings;
};

class IAssetMeta {
public:
    virtual ~IAssetMeta() {}

    IAssetMeta(const eAssetType type, IGuid * const guid, IAssetImporter * const importer) : _AssetType(type), _Guid(guid), _AssetImporter(importer) {}

    const eAssetType _AssetType;
    const IGuid * const _Guid;
    IAssetImporter * const _AssetImporter;
};

class IAsset {
public:
    virtual ~IAsset() {}
    IAsset(const IAssetMeta * const meta) : _Meta(meta) {}

    const IAssetMeta * const _Meta;
};

enum class eAssetEvent : uint8_t {
    Create,
    Delete,
    Modify
};

class IAssetsManager : public GameSprout::IGameSproutModule {
public:
    virtual ~IAssetsManager() {}

    virtual IAsset * QueryAsset(const std::string & guid) const = 0;
};

#endif //__IAssetsManager_h__
