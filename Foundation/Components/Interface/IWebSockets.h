#ifndef WEB_SOCKETS_COMPONENT_INTERFACE_H
#define WEB_SOCKETS_COMPONENT_INTERFACE_H

#include "IFoundation.h"
#include "IWebSocketsApi.h"

namespace Foundation {
    class IWebSockets : public IFoundation, public IWebSocketsApi {
    public:
        virtual ~IWebSockets() {}
    };
}

#endif // WEB_SOCKETS_COMPONENT_INTERFACE_H
