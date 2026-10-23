#ifndef __ITaskManager_h__
#define __ITaskManager_h__

#include "IFoundation.h"
#include "ITaskApi.h"

namespace Foundation {
    class ITaskManager : public IFoundation, public ITaskApi {
    public:
        virtual ~ITaskManager() {}
    };
}

#endif // __ITaskManager_h__
