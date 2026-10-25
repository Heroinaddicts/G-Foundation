#ifndef __TextureImportFactory_h__
#define __TextureImportFactory_h__

#include "Header.h"


class TextureImporter : public ITextureImporter {
public:
    virtual ~TextureImporter() {}
};

class TextureMeta : public IAssetMeta {
public:
    virtual ~TextureMeta() {}
};

class TextureAsset : public Asset<TextureMeta> {
public:
    virtual ~TextureAsset() {}
};


inline static IAssetImporter * OnTextureAssetCreate() {

}

#endif //__TextureImportFactory_h__
