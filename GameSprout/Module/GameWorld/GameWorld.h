#ifndef __GameWorld_h__
#define __GameWorld_h__

#include "Header.h"

class GameWorld : public IGameWorld {
public:
    virtual ~GameWorld() {}

private:
    bool Initialize(GameSproutApi * const api) override;
    bool Launch(GameSproutApi * const api) override;
    void LaunchFinished(GameSproutApi * const api) override;
    void Update(GameSproutApi * const api) override;
    void Release(GameSproutApi * const api) override;
};

#endif //__GameWorld_h__
