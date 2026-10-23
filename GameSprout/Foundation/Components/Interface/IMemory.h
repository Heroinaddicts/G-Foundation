#ifndef __IMemory_h__
#define __IMemory_h__


#include "IFoundation.h"

namespace Foundation {
    class IMemory : public IFoundation, public IMemoryApi {
    public:
        virtual ~IMemory() {}
    };
}

#endif //__IMemory_h__
