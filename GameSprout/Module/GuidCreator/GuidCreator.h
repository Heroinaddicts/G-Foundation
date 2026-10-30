#ifndef __GuidCreator_h__
#define __GuidCreator_h__

#include "Header.h"

class GuidCreator : public IGuidCreator {
public:
    virtual ~GuidCreator() {}

    Guid CreateGuid() override;

private:
    void BeforeInitialize(GameSproutModuleApi * const api) override;
    bool Initialize(GameSproutModuleApi * const api) override;
    bool Launch(GameSproutModuleApi * const api) override;
    void LaunchFinished(GameSproutModuleApi * const api) override;
    void Update(GameSproutModuleApi * const api) override;
    void Release(GameSproutModuleApi * const api) override;
};

#endif //__GuidCreator_h__
