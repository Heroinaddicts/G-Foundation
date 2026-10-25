#ifndef __FolderAssetFactory_hpp__
#define __FolderAssetFactory_hpp__

#include "Header.h"


class FolderImporter : public IFolderImporter {
public:
    virtual ~FolderImporter() {}
};

class FolderMeta : public IAssetMeta {
public:
    virtual ~FolderMeta() {}
};

class FolderAsset : public Asset<FolderMeta> {
public:
    virtual ~FolderAsset() {}
};


inline static IAssetImporter * OnFolderAssetCreate() {

}

#endif //__FolderAssetFactory_hpp__
