#ifndef __BuildSetting_h__
#define __BuildSetting_h__

#include "Header.h"

class BuildSetting : public IBuildSetting {
public:
    virtual ~BuildSetting() {}

private:
    void BeforeInitialize(GameSproutModuleApi * const api) override;
    bool Initialize(GameSproutModuleApi * const api) override;
    bool Launch(GameSproutModuleApi * const api) override;
    void LaunchFinished(GameSproutModuleApi * const api) override;
    void Update(GameSproutModuleApi * const api) override;
    void Release(GameSproutModuleApi * const api) override;
};

#endif //__BuildSetting_h__
