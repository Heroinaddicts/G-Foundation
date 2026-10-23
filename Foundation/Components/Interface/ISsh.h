#ifndef __ISsh_h__
#define __ISsh_h__

#include "IFoundation.h"

namespace Foundation {
    class ISsh : public IFoundation, public ISshApi {
    public:
        virtual ~ISsh() {}
    };
}

#endif //__iLogic_h__
