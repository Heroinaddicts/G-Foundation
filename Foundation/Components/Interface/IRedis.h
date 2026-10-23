#ifndef REDIS_COMPONENT_INTERFACE_H
#define REDIS_COMPONENT_INTERFACE_H

#include "IFoundation.h"
#include "IRedisApi.h"


namespace Foundation {
    class IRedis : public IFoundation, public IRedisApi {
    public:
        virtual ~IRedis() {}
    };
}

#endif // REDIS_COMPONENT_INTERFACE_H
