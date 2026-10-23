#ifndef __AssetsPipeline_h__
#define __AssetsPipeline_h__

#include "Header.h"

class AssetsPipeline : public IAssetsPipeline {
public:
    virtual ~AssetsPipeline() {}

private:
    void BeforeInitialize(GameSproutApi * const api) override;
    bool Initialize(GameSproutApi * const api) override;
    bool Launch(GameSproutApi * const api) override;
    void LaunchFinished(GameSproutApi * const api) override;
    void Update(GameSproutApi * const api) override;
    void Release(GameSproutApi * const api) override;
};

#endif //__AssetsPipeline_h__
