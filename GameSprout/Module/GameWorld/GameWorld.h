#ifndef __GameWorld_h__
#define __GameWorld_h__

#include "Header.h"

class GameWorld : public IGameWorld {
public:
    virtual ~GameWorld() {}

private:
    bool Initialize(GameSproutModuleApi * const api) override;
    bool Launch(GameSproutModuleApi * const api) override;
    void LaunchFinished(GameSproutModuleApi * const api) override;
    void Update(GameSproutModuleApi * const api) override;
    void Release(GameSproutModuleApi * const api) override;
};

#endif //__GameWorld_h__
