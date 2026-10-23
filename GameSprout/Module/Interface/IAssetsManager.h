#ifndef __IAssetsManager_h__
#define __IAssetsManager_h__

#include "GameSproutApi.h"
#include "GRHIApi.h"

enum class eAssetType : uint8_t {

};

class IAsset {
public:
    virtual ~IAsset() {}
    IAsset(const std::string & guid) : _Guid(guid) {}

    const std::string _Guid;
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
