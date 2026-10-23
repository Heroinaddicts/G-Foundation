#ifndef __IAssetsPipeline_h__
#define __IAssetsPipeline_h__

#include "GameSproutApi.h"
#include "GRHIApi.h"

class IAssetImport {
public:
    virtual ~IAssetImport() {}
};

class IAssetImportContext {
public:
    virtual ~IAssetImportContext() {}
};

class IAssetsPipeline : public GameSprout::IGameSproutModule {
public:
    virtual ~IAssetsPipeline() {}
};

#endif //__IAssetsPipeline_h__
