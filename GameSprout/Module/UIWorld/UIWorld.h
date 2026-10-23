#ifndef __UIWorld_h__
#define __UIWorld_h__

#include "Header.h"

class UIWorld : public IUIWorld {
public:
    virtual ~UIWorld() {}

private:
    bool Initialize(GameSproutApi * const api) override;
    bool Launch(GameSproutApi * const api) override;
    void LaunchFinished(GameSproutApi * const api) override;
    void Update(GameSproutApi * const api) override;
    void Release(GameSproutApi * const api) override;
};

#endif //__UIWorld_h__
