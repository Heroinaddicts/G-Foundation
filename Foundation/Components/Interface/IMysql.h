#ifndef __IMysql_h__
#define __IMysql_h__

#include "IFoundation.h"

namespace Foundation {
    class IMysql : public IFoundation, public IMysqlApi {
    public:
        virtual ~IMysql() {}
    };
}

#endif //__iLogic_h__
