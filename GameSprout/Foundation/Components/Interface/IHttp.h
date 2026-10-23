#ifndef __iHttp_h__
#define __iHttp_h__

#include "IFoundation.h"

namespace Foundation {
    class IHttp : public IFoundation, public IHttpApi {
    public:
        virtual ~IHttp() {}

    };
}

#endif //__iHttp_h__
