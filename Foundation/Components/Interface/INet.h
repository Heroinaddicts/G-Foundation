#ifndef __INet_h__
#define __INet_h__

#include "IFoundation.h"
#include "INetApi.h"

namespace Foundation {
    class INet : public IFoundation, public INetApi {
    public:
        virtual ~INet() {}
    };
}

#endif //__iLogic_h__
