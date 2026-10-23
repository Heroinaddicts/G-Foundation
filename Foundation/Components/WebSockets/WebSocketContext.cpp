#include "WebSocketContext.h"

#include "WebSocketPipe.h"
#include "WebSockets.h"

#include <cstring>
#include <new>
#include <vector>

namespace Foundation {
    namespace {
        void PushWebSocketEvent(
            eWebSocketEvent eventType,
            WebSocketContext* context,
            WebSocketPipe* pipe,
            IWebSocketSession* session = nullptr) {
            if (!context) {
                return;
            }
            WbsEvent* event = new (std::nothrow) WbsEvent();
            if (!event) {
                return;
            }
            event->_Event = eventType;
            event->_Context = context;
            event->_Pipe = pipe;
            event->_Session = session;
            context->PushEvent(event);
        }

        int WriteWebSocketBuffer(lws* wsi, WebSocketPipe* pipe) {
            if (!wsi || !pipe || pipe->IsClosed()) {
                return -1;
            }

            const int messageWriteResult = pipe->WriteNextMessage(wsi);
            if (messageWriteResult < 0) {
                return -1;
            }
            if (messageWriteResult > 0) {
                // A writable callback is edge-triggered rather than counted. Always request
                // the next callback so another framed message or legacy byte-stream payload
                // queued concurrently cannot be left behind after this message completes.
                lws_callback_on_writable(wsi);
                return 0;
            }

            pipe->_SendBuffer.Read(
                [wsi](const void* data, int size) -> int {
                    if (!data || size <= 0) {
                        return 0;
                    }
                    std::vector<unsigned char> buffer(static_cast<std::size_t>(LWS_PRE + size));
                    std::memcpy(buffer.data() + LWS_PRE, data, static_cast<std::size_t>(size));
                    const int written = lws_write(
                        wsi,
                        buffer.data() + LWS_PRE,
                        static_cast<std::size_t>(size),
                        LWS_WRITE_BINARY);
                    return written > 0 ? written : 0;
                });
            return 0;
        }
    }

    WebSocketContext* WebSocketContext::Create(
        WebSockets* owner,
        FoundationApi* foundation,
        eWebSocketContextType type,
        void* user,
        const char* interfaceName,
        const char* ca,
        const char* cert,
        const char* key,
        int port,
        const char* subproto) {
        if (!owner || !foundation) {
            return nullptr;
        }

        WebSocketContext* context = new (std::nothrow) WebSocketContext(
            owner,
            foundation,
            type,
            user,
            interfaceName,
            ca,
            cert,
            key,
            port,
            subproto);
        if (!context || !context->Context() || !context->Start()) {
            if (context) {
                delete context;
            }
            return nullptr;
        }
        return context;
    }

    void WebSocketContext::Release(WebSocketContext* context) {
        if (!context) {
            return;
        }
        context->RequestStop();
        if (context->_LwsContext) {
            lws_cancel_service(context->_LwsContext);
        }
        context->Wait();
        delete context;
    }

    WebSocketContext::WebSocketContext(
        WebSockets* owner,
        FoundationApi* foundation,
        eWebSocketContextType type,
        void* user,
        const char* interfaceName,
        const char* ca,
        const char* cert,
        const char* key,
        int port,
        const char* subproto)
        : _Owner(owner),
        _Foundation(foundation),
        _Type(type),
        _InterfaceName(interfaceName ? interfaceName : ""),
        _SubProto(subproto&&* subproto ? subproto : "Foundation"),
        _Port(port),
        _User(user) {
        if (_Foundation) {
            if (ca && *ca) {
                _CaPem = FoundationUtils::SafeSystem::File::LoadFileAsString(ca);
            }
            if (cert && *cert) {
                _CertPem = FoundationUtils::SafeSystem::File::LoadFileAsString(cert);
            }
            if (key && *key) {
                _KeyPem = FoundationUtils::SafeSystem::File::LoadFileAsString(key);
            }
        }

        _Protocols = new (std::nothrow) lws_protocols[2]{};
        if (!_Protocols) {
            return;
        }
        _Protocols[0].name = _SubProto.c_str();
        _Protocols[0].callback = WebSocketContext::CallbackEcho;
        _Protocols[0].per_session_data_size = 0;
        _Protocols[0].rx_buffer_size = 8192;

        lws_context_creation_info info{};
        info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        info.protocols = _Protocols;
        info.user = this;
        info.count_threads = 1;
        info.iface = _InterfaceName.empty() ? nullptr : _InterfaceName.c_str();

        if (_Type == eWebSocketContextType::Client) {
            info.port = CONTEXT_PORT_NO_LISTEN;
            if (!_CaPem.empty()) {
                info.client_ssl_ca_mem = _CaPem.data();
                info.client_ssl_ca_mem_len = static_cast<unsigned int>(_CaPem.size());
            }
            if (!_CertPem.empty()) {
                info.client_ssl_cert_mem = _CertPem.data();
                info.client_ssl_cert_mem_len = static_cast<unsigned int>(_CertPem.size());
            }
            if (!_KeyPem.empty()) {
                info.client_ssl_key_mem = _KeyPem.data();
                info.client_ssl_key_mem_len = static_cast<unsigned int>(_KeyPem.size());
            }
        }
        else {
            info.port = _Port;
            info.options |= LWS_SERVER_OPTION_VALIDATE_UTF8;
            if (!_CertPem.empty() && !_KeyPem.empty()) {
                info.server_ssl_cert_mem = _CertPem.data();
                info.server_ssl_cert_mem_len = static_cast<unsigned int>(_CertPem.size());
                info.server_ssl_private_key_mem = _KeyPem.data();
                info.server_ssl_private_key_mem_len = static_cast<unsigned int>(_KeyPem.size());
            }
        }

        _LwsContext = lws_create_context(&info);
        if (!_LwsContext) {
            ErrorLog(_Foundation, "lws_create_context failed");
        }
    }

    WebSocketContext::~WebSocketContext() {
        if (_LwsContext) {
            lws_context_destroy(_LwsContext);
            _LwsContext = nullptr;
        }

        ProcessRequests();
        delete[] _Protocols;
        _Protocols = nullptr;
    }

    bool WebSocketContext::Start() {
        return FoundationUtils::SafeThread::Start();
    }

    void WebSocketContext::PushRequest(WbsRequest* request) {
        if (!request) {
            return;
        }
        _WbsRequestQueue.Push(request);
        if (_LwsContext) {
            lws_cancel_service(_LwsContext);
        }
    }

    void WebSocketContext::PushEvent(WbsEvent* event) {
        if (!event) {
            return;
        }
        if (_Owner) {
            _Owner->PushEvent(event);
        }
        else {
            delete event;
        }
    }

    void WebSocketContext::Run(const int, void* const) {
        if (!_LwsContext) {
            return;
        }
        while (!IsStopRequested()) {
            lws_service(_LwsContext, 50);
            ProcessRequests();
        }
        ProcessRequests();
    }

    void WebSocketContext::ProcessRequests() {
        WbsRequest* request = nullptr;
        while (_WbsRequestQueue.Pull(request)) {
            if (request && request->_Pipe) {
                switch (request->_Request) {
                case eWebSocketRequest::Send:
                case eWebSocketRequest::Close:
                    if (request->_Pipe->_Wsi) {
                        lws_callback_on_writable(request->_Pipe->_Wsi);
                    }
                    break;
                case eWebSocketRequest::ReleasePipe:
                    WebSocketPipe::Release(request->_Pipe);
                    break;
                default:
                    break;
                }
            }
            if (request) {
                delete request;
            }
        }
    }

    int WebSocketContext::CallbackEcho(
        lws* wsi,
        lws_callback_reasons reason,
        void* user,
        void* in,
        size_t len) {
        WebSocketContext* context = static_cast<WebSocketContext*>(
            lws_context_user(lws_get_context(wsi)));

        switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            WebSocketPipe* pipe = WebSocketPipe::Create(wsi, context, false);
            if (!pipe) {
                return -1;
            }
            lws_set_wsi_user(wsi, pipe);
            PushWebSocketEvent(eWebSocketEvent::Accepted, context, pipe);
            return 0;
        }
        case LWS_CALLBACK_CLIENT_ESTABLISHED: {
            IWebSocketSession* session = static_cast<IWebSocketSession*>(user);
            WebSocketPipe* pipe = WebSocketPipe::Create(wsi, context, true);
            if (!pipe) {
                return -1;
            }
            lws_set_wsi_user(wsi, pipe);
            PushWebSocketEvent(eWebSocketEvent::Connected, context, pipe, session);
            return 0;
        }
        case LWS_CALLBACK_RECEIVE:
        case LWS_CALLBACK_CLIENT_RECEIVE: {
            WebSocketPipe* pipe = static_cast<WebSocketPipe*>(user);
            if (pipe && in && len > 0) {
                pipe->_RecvBuffer.Write(in, static_cast<int>(len), __FILE__, __LINE__);
                PushWebSocketEvent(eWebSocketEvent::Receive, context, pipe);
            }
            return 0;
        }
        case LWS_CALLBACK_SERVER_WRITEABLE:
        case LWS_CALLBACK_CLIENT_WRITEABLE:
            return WriteWebSocketBuffer(wsi, static_cast<WebSocketPipe*>(user));
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
            IWebSocketSession* session = static_cast<IWebSocketSession*>(user);
            PushWebSocketEvent(eWebSocketEvent::ConnectFailed, context, nullptr, session);
            return 0;
        }
        case LWS_CALLBACK_CLIENT_CLOSED:
        case LWS_CALLBACK_CLOSED: {
            WebSocketPipe* pipe = static_cast<WebSocketPipe*>(user);
            if (pipe && pipe->OnClose(false)) {
                pipe->_Wsi = nullptr;
                PushWebSocketEvent(eWebSocketEvent::Disconnected, context, pipe);
            }
            return 0;
        }
        default:
            return 0;
        }
    }
}
