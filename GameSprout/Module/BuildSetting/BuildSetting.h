#ifndef __BuildSetting_h__
#define __BuildSetting_h__

#include "Header.h"

class BuildSetting : public IBuildSetting {
public:
    virtual ~BuildSetting() {}

private:
    void BeforeInitialize(GameSproutApi * const api) override;
    bool Initialize(GameSproutApi * const api) override;
    bool Launch(GameSproutApi * const api) override;
    void LaunchFinished(GameSproutApi * const api) override;
    void Update(GameSproutApi * const api) override;
    void Release(GameSproutApi * const api) override;
};

#endif //__BuildSetting_h__
