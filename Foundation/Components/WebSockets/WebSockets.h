#ifndef WEB_SOCKETS_H
#define WEB_SOCKETS_H

#include "Header.h"

namespace Foundation {
    class WebSockets final : public IWebSockets {
    public:
        ~WebSockets() override;

        void ReleaseAccept(WebSocketsAccept* accepter);
        void PushEvent(WbsEvent* event);

    private:
        friend class Foundation;
        WebSockets() = default;

        bool Initialize(FoundationApi* foundation) override;
        bool Launch(FoundationApi* foundation) override;
        void Release(FoundationApi* foundation) override;
        void EarlyUpdate(FoundationApi* foundation) override;
        void Update(FoundationApi* foundation) override;
        void LaterUpdate(FoundationApi* foundation) override;

        bool LaunchSession(
            IWebSocketSession* session,
            const char* host,
            int port,
            const char* ca,
            const char* cert,
            const char* privateKey,
            const char* subproto) override;
        bool LaunchServer(
            IWebSocketsServer* server,
            const char* ip,
            int port,
            const char* cert,
            const char* privateKey,
            const char* subproto) override;

        void AddClientContext(WebSocketContext* context);
        void ReleaseClientContext(WebSocketContext* context);
        void DrainPendingEvents();

        FoundationApi* _Foundation = nullptr;
        bool _Launched = false;
        FoundationUtils::SpinLock _ContextsLock;
        FoundationUtils::SafeQueue::SpscQueue<WbsEvent*> _EventQueue;
        std::vector<WebSocketContext*> _ClientContexts;
        std::vector<WebSocketsAccept*> _Acceptors;
    };
}

#endif // WEB_SOCKETS_H
