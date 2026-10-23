#ifndef __IGuidCreator_h__
#define __IGuidCreator_h__

#include "Define.h"

class IGuid {
public:
    virtual ~IGuid() {}


};

class IGuidCreator : public GameSprout::IGameSproutModule {
public:
    virtual ~IGuidCreator() {}
};

#endif //__IGuidCreator_h__
