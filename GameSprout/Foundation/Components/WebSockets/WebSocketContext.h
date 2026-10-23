#ifndef WEB_SOCKET_CONTEXT_H
#define WEB_SOCKET_CONTEXT_H

#include "Header.h"

namespace Foundation {
    class WebSocketContext final : public FoundationUtils::SafeThread {
    public:
        ~WebSocketContext() override;

        static WebSocketContext* Create(
            WebSockets* owner,
            FoundationApi* foundation,
            eWebSocketContextType type,
            void* user,
            const char* interfaceName,
            const char* ca,
            const char* cert,
            const char* key,
            int port = CONTEXT_PORT_NO_LISTEN,
            const char* subproto = "Foundation");
        static void Release(WebSocketContext* context);

        lws_context* Context() const {
            return _LwsContext;
        }
        eWebSocketContextType Type() const {
            return _Type;
        }

        void PushRequest(WbsRequest* request);
        void PushEvent(WbsEvent* event);

        template<typename T>
        T* User() const {
            return static_cast<T*>(_User);
        }

        void Run(const int threadIndex, void* const context) override;

    private:
        WebSocketContext(
            WebSockets* owner,
            FoundationApi* foundation,
            eWebSocketContextType type,
            void* user,
            const char* interfaceName,
            const char* ca,
            const char* cert,
            const char* key,
            int port,
            const char* subproto);

        bool Start();
        void ProcessRequests();
        static int CallbackEcho(lws* wsi, lws_callback_reasons reason, void* user, void* in, size_t len);

        WebSockets* _Owner = nullptr;
        FoundationApi* _Foundation = nullptr;
        eWebSocketContextType _Type;
        std::string _InterfaceName;
        std::string _CaPem;
        std::string _CertPem;
        std::string _KeyPem;
        std::string _SubProto;
        int _Port;
        lws_context* _LwsContext = nullptr;
        lws_protocols* _Protocols = nullptr;
        void* _User = nullptr;
        FoundationUtils::SafeQueue::SpscQueue<WbsRequest*> _WbsRequestQueue;
    };
}

#endif // WEB_SOCKET_CONTEXT_H
