#ifndef COMPONENT_XZIP_H
#define COMPONENT_XZIP_H

#include "IFoundation.h"
#include "IZipApi.h"

namespace Foundation {
    class IXZip : public IFoundation, public IZipApi {
    public:
        virtual ~IXZip() {}
    };
}

#endif // COMPONENT_XZIP_H
