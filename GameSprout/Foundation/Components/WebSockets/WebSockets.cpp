#include "WebSockets.h"

#include "Foundation.h"
#include "WebSocketContext.h"
#include "WebSocketPipe.h"
#include "WebSocketsAccept.h"

#include <algorithm>
#include <new>
#include <mutex>
#include <set>

namespace Foundation {
    WebSockets::~WebSockets() {
    }

    bool WebSockets::Initialize(FoundationApi* foundation) {
        _Foundation = foundation;
        _Launched = false;
        if (!_Foundation) {
            return false;
        }

        static std::once_flag logConfiguration;
        std::call_once(logConfiguration, []() {
            lws_set_log_level(
                LLL_ERR | LLL_WARN | LLL_NOTICE,
                [](int, const char* line) {
                    if (line) {
                        FError("libwebsockets: %s", line);
                    }
                });
        });
        return true;
    }

    bool WebSockets::Launch(FoundationApi* foundation) {
        if (foundation != _Foundation || _Launched) {
            return false;
        }
        _Launched = true;
        return true;
    }

    void WebSockets::Release(FoundationApi* foundation) {
        if (!_Foundation) {
            return;
        }

        LaterUpdate(_Foundation);

        std::vector<WebSocketsAccept*> acceptors;
        std::vector<WebSocketContext*> clientContexts;
        {
            AUTO_LOCK(_ContextsLock);
            acceptors.swap(_Acceptors);
            clientContexts.swap(_ClientContexts);
        }

        for (WebSocketsAccept* accepter : acceptors) {
            if (accepter) {
                accepter->Destroy();
            }
        }
        for (WebSocketContext* context : clientContexts) {
            WebSocketContext::Release(context);
        }

        DrainPendingEvents();
        _Launched = false;
        _Foundation = nullptr;
    }

    void WebSockets::EarlyUpdate(FoundationApi* foundation) {
    }

    void WebSockets::Update(FoundationApi* foundation) {
    }

    void WebSockets::LaterUpdate(FoundationApi* foundation) {
        if (!_Foundation) {
            return;
        }

        const UInt64 beginTick = FoundationUtils::SafeSystem::Time::GetMicroSecond();
        WbsEvent* event = nullptr;
        while (_EventQueue.Pull(event)) {
            if (!event) {
                continue;
            }

            switch (event->_Event) {
            case eWebSocketEvent::Accepted: {
                WebSocketsAccept* accepter = event->_Context
                    ? event->_Context->User<WebSocketsAccept>()
                    : nullptr;
                IWebSocketsServer* server = accepter ? accepter->Server() : nullptr;
                IWebSocketSession* session = server && event->_Pipe
                    ? server->OnMallocWebSocketSession(event->_Pipe->RemoteIp(), event->_Pipe->RemotePort())
                    : nullptr;
                if (session && event->_Pipe) {
                    event->_Pipe->SetSession(session);
                    session->OnWebSocketSessionEvent(eWebSocketSessionEvent::Connected);
                }
                else if (event->_Pipe) {
                    event->_Pipe->Close();
                }
                break;
            }
            case eWebSocketEvent::Connected:
                if (event->_Pipe && event->_Session) {
                    event->_Pipe->SetSession(event->_Session);
                    event->_Session->OnWebSocketSessionEvent(eWebSocketSessionEvent::Connected);
                }
                break;
            case eWebSocketEvent::ConnectFailed:
                if (event->_Session) {
                    event->_Session->OnWebSocketSessionEvent(eWebSocketSessionEvent::Connectfaild);
                }
                ReleaseClientContext(event->_Context);
                break;
            case eWebSocketEvent::Receive:
                if (event->_Pipe && !event->_Pipe->IsClosed() && !event->_Pipe->IsPaused()) {
                    IWebSocketSession* session = event->_Pipe->Session();
                    while (session && !event->_Pipe->IsClosed() && !event->_Pipe->IsPaused()
                        && event->_Pipe->_RecvBuffer.Read(
                            [session](const void* data, int size) -> int {
                                return size > 0 ? session->OnReceive(data, size) : 0;
                            })) {
                    }
                }
                break;
            case eWebSocketEvent::Disconnected: {
                IWebSocketSession* session = event->_Pipe ? event->_Pipe->DetachSession() : nullptr;
                if (session) {
                    session->OnWebSocketSessionEvent(eWebSocketSessionEvent::Disconnected);
                }
                if (event->_Pipe) {
                    WebSocketPipe::Release(event->_Pipe);
                    event->_Pipe = nullptr;
                }
                if (event->_Context && event->_Context->Type() == eWebSocketContextType::Client) {
                    ReleaseClientContext(event->_Context);
                }
                break;
            }
            default:
                break;
            }

            delete event;
            if (FoundationUtils::SafeSystem::Time::GetMicroSecond() - beginTick > 1000) {
                break;
            }
        }
    }

    bool WebSockets::LaunchSession(
        IWebSocketSession* session,
        const char* host,
        int port,
        const char* ca,
        const char* cert,
        const char* privateKey,
        const char* subproto) {
        if (!_Launched || !_Foundation || !session || !host || !*host || port <= 0 || port > 65535) {
            return false;
        }

        const char* protocol = subproto && *subproto ? subproto : "Foundation";
        WebSocketContext* context = WebSocketContext::Create(
            this,
            _Foundation,
            eWebSocketContextType::Client,
            this,
            nullptr,
            ca,
            cert,
            privateKey,
            CONTEXT_PORT_NO_LISTEN,
            protocol);
        if (!context) {
            session->OnWebSocketSessionEvent(eWebSocketSessionEvent::Connectfaild);
            return false;
        }
        AddClientContext(context);

        lws_client_connect_info info{};
        info.context = context->Context();
        info.port = port;
        info.address = host;
        info.path = "/";
        info.host = host;
        info.origin = host;
        info.protocol = protocol;
        info.local_protocol_name = protocol;
        info.userdata = session;
        const bool useTls = (ca && *ca) || (cert && *cert) || (privateKey && *privateKey);
        info.ssl_connection = useTls ? LCCSCF_USE_SSL : 0;

        if (!lws_client_connect_via_info(&info)) {
            ReleaseClientContext(context);
            session->OnWebSocketSessionEvent(eWebSocketSessionEvent::Connectfaild);
            ErrorLog(_Foundation, "WebSocket client connection failed %s:%d", host, port);
            return false;
        }
        return true;
    }

    bool WebSockets::LaunchServer(
        IWebSocketsServer* server,
        const char* ip,
        int port,
        const char* cert,
        const char* privateKey,
        const char* subproto) {
        if (!_Launched || !_Foundation || !server || port <= 0 || port > 65535 || server->Accepter()) {
            return false;
        }

        WebSocketsAccept* accepter = new (std::nothrow) WebSocketsAccept(
            this,
            _Foundation,
            server,
            ip,
            cert,
            privateKey,
            port,
            subproto && *subproto ? subproto : "Foundation");
        if (!accepter || !accepter->IsValid()) {
            if (accepter) {
                accepter->Destroy();
            }
            return false;
        }

        AUTO_LOCK(_ContextsLock);
        _Acceptors.push_back(accepter);
        return true;
    }

    void WebSockets::ReleaseAccept(WebSocketsAccept* accepter) {
        if (!accepter) {
            return;
        }
        {
            AUTO_LOCK(_ContextsLock);
            const auto iterator = std::find(_Acceptors.begin(), _Acceptors.end(), accepter);
            if (iterator == _Acceptors.end()) {
                return;
            }
            _Acceptors.erase(iterator);
        }
        accepter->Destroy();
        DrainPendingEvents();
    }

    void WebSockets::PushEvent(WbsEvent* event) {
        if (event) {
            _EventQueue.Push(event);
        }
    }

    void WebSockets::AddClientContext(WebSocketContext* context) {
        if (!context) {
            return;
        }
        AUTO_LOCK(_ContextsLock);
        _ClientContexts.push_back(context);
    }

    void WebSockets::ReleaseClientContext(WebSocketContext* context) {
        if (!context) {
            return;
        }
        {
            AUTO_LOCK(_ContextsLock);
            const auto iterator = std::find(_ClientContexts.begin(), _ClientContexts.end(), context);
            if (iterator == _ClientContexts.end()) {
                return;
            }
            _ClientContexts.erase(iterator);
        }
        WebSocketContext::Release(context);
    }

    void WebSockets::DrainPendingEvents() {
        std::set<WebSocketPipe*> releasedPipes;
        WbsEvent* event = nullptr;
        while (_EventQueue.Pull(event)) {
            if (event && event->_Pipe && releasedPipes.insert(event->_Pipe).second) {
                IWebSocketSession* session = event->_Pipe->DetachSession();
                if (session) {
                    session->OnWebSocketSessionEvent(eWebSocketSessionEvent::Disconnected);
                }
                WebSocketPipe::Release(event->_Pipe);
            }
            if (event) {
                delete event;
            }
        }
    }
}
