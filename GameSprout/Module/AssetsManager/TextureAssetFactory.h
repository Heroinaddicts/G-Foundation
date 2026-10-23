#ifndef __TextureImportFactory_h__
#define __TextureImportFactory_h__

#include "Header.h"

class TextureAssetMeta;

class TextureImport : public IAssetImporter {
public:
    virtual ~TextureImport() {}
    TextureImport(Asset<TextureAssetMeta> * const asset) : _Asset(asset) {}

    IAsset * GetAsset() const override {
        return _Asset;
    }

    void Import(const fAssetImportCallback callback) override {

    }


    Asset<TextureAssetMeta> * const _Asset;
};

class TextureAssetMeta : public IAssetMeta {
public:
    virtual ~TextureAssetMeta() {}


    virtual IAsset * GetAsset() const = 0;
    virtual IAssetImporter * GetImport() const = 0;
};

static IAssetImporter * OnTextureAssetCreate(IAsset * asset);

#endif //__TextureImportFactory_h__
