#ifndef __IRuntime_h__
#define __IRuntime_h__

#include "GameSproutApi.h"
#include "GRHIApi.h"

class IRuntime : public GameSprout::IGameSproutModule {
public:
    virtual ~IRuntime() {}
};

#endif //__IRuntime_h__
