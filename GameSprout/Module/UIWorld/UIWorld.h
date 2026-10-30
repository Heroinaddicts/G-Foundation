#ifndef __UIWorld_h__
#define __UIWorld_h__

#include "Header.h"

class UIWorld : public IUIWorld {
public:
    virtual ~UIWorld() {}

private:
    bool Initialize(GameSproutModuleApi * const api) override;
    bool Launch(GameSproutModuleApi * const api) override;
    void LaunchFinished(GameSproutModuleApi * const api) override;
    void Update(GameSproutModuleApi * const api) override;
    void Release(GameSproutModuleApi * const api) override;
};

#endif //__UIWorld_h__
