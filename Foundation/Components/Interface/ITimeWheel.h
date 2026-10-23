#ifndef TIME_WHEEL_COMPONENT_INTERFACE_H
#define TIME_WHEEL_COMPONENT_INTERFACE_H

#include "IFoundation.h"
#include "ITimerApi.h"

namespace Foundation {
    class ITimeWheel : public IFoundation, public ITimerApi {
    public:
        virtual ~ITimeWheel() {}
    };
}

#endif // TIME_WHEEL_COMPONENT_INTERFACE_H
