#ifndef __IAssetsManager_h__
#define __IAssetsManager_h__

#include "Define.h"

class IGuid;
class IAsset;
class IAssetMeta;

class IAssetImporter {
public:
    virtual ~IAssetImporter() {}

    virtual IAsset * GetAsset() const = 0;
    typedef std::function<void(const bool, const std::string &, IAssetImporter * const)> fAssetImportCallback;
    virtual void Import(const fAssetImportCallback callback) = 0;
};

class IAssetImportSettings {
public:
    virtual ~IAssetImportSettings() {}

    virtual IAssetImporter * GetImporter() const = 0;
};

class ITextureImportSettings : public IAssetImportSettings {
public:
    virtual ~ITextureImportSettings() {}
};

class ITextureImporter : public IAssetImporter {
public:
    virtual ~ITextureImporter() {}
    ITextureImporter(ITextureImportSettings * const settings) : _Settings(settings) {}

    ITextureImportSettings * const _Settings;
};

class IAssetMeta {
public:
    virtual ~IAssetMeta() {}

    IAssetMeta(const eAssetType type, IGuid * const guid) : _AssetType(type), _Guid(guid) {}

    virtual IAsset * GetAsset() const = 0;
    virtual IAssetImporter * GetImport() const = 0;

    const eAssetType _AssetType;
    const IGuid * const _Guid;
};

class IAsset {
public:
    virtual ~IAsset() {}

    virtual IAssetMeta * GetMeta() const = 0;
};

enum class eAssetEvent : uint8_t {
    Create,
    Delete,
    Modify
};

class IAssetsManager : public GameSprout::IGameSproutModule {
public:
    virtual ~IAssetsManager() {}

    virtual IAsset * QueryAsset(const IGuid * const guid) const = 0;
};

#endif //__IAssetsManager_h__
