#ifndef COMPONENT_SYSTEM_INTERFACE_H
#define COMPONENT_SYSTEM_INTERFACE_H

#include "IFoundation.h"

namespace Foundation {
    class ISystem : public IFoundation, public ISystemApi {
    public:
        ~ISystem() override {}
    };
}

#endif // COMPONENT_SYSTEM_INTERFACE_H
