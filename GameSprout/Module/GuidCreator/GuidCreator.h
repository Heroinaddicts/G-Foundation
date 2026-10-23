#ifndef __GuidCreator_h__
#define __GuidCreator_h__

#include "Header.h"

class GuidCreator : public IGuidCreator {
public:
    virtual ~GuidCreator() {}

private:
    void BeforeInitialize(GameSproutApi * const api) override;
    bool Initialize(GameSproutApi * const api) override;
    bool Launch(GameSproutApi * const api) override;
    void LaunchFinished(GameSproutApi * const api) override;
    void Update(GameSproutApi * const api) override;
    void Release(GameSproutApi * const api) override;
};

#endif //__GuidCreator_h__
