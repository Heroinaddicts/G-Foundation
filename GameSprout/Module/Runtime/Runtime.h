#ifndef __Runtime_h__
#define __Runtime_h__

#include "Header.h"

class Runtime : public IRuntime {
public:
    virtual ~Runtime() {}

private:
    void BeforeInitialize(GameSproutApi * const api) override;
    bool Initialize(GameSproutApi * const api) override;
    bool Launch(GameSproutApi * const api) override;
    void LaunchFinished(GameSproutApi * const api) override;
    void Update(GameSproutApi * const api) override;
    void Release(GameSproutApi * const api) override;
};

#endif //__Runtime_h__
