#ifndef WEB_SOCKETS_ACCEPT_H
#define WEB_SOCKETS_ACCEPT_H

#include "Header.h"

namespace Foundation {
    class WebSocketsAccept final : public IWebSocketServerAccept {
    public:
        WebSocketsAccept(
            WebSockets* owner,
            FoundationApi* foundation,
            IWebSocketsServer* server,
            const char* listenIp,
            const char* cert,
            const char* key,
            int port,
            const char* subproto);
        ~WebSocketsAccept() override;

        bool IsValid() const;
        void Release() override;

        IWebSocketsServer* Server() const {
            return _Server;
        }
        WebSocketContext* Context() const {
            return _Context;
        }

    private:
        friend class WebSockets;
        void Destroy();

        WebSockets* _Owner = nullptr;
        IWebSocketsServer* _Server = nullptr;
        WebSocketContext* _Context = nullptr;
    };
}

#endif // WEB_SOCKETS_ACCEPT_H
