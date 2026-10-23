#include "WebSocketsAccept.h"

#include "WebSocketContext.h"
#include "WebSockets.h"

namespace Foundation {
    WebSocketsAccept::WebSocketsAccept(
        WebSockets* owner,
        FoundationApi* foundation,
        IWebSocketsServer* server,
        const char* listenIp,
        const char* cert,
        const char* key,
        int port,
        const char* subproto)
        : _Owner(owner),
          _Server(server),
          _Context(WebSocketContext::Create(
              owner,
              foundation,
              eWebSocketContextType::Server,
              this,
              listenIp,
              nullptr,
              cert,
              key,
              port,
              subproto)) {
        if (_Server && _Context) {
            _Server->SetAccepter(this);
        }
    }

    WebSocketsAccept::~WebSocketsAccept() {
    }

    bool WebSocketsAccept::IsValid() const {
        return _Owner && _Server && _Context;
    }

    void WebSocketsAccept::Release() {
        if (_Owner) {
            _Owner->ReleaseAccept(this);
        }
    }

    void WebSocketsAccept::Destroy() {
        WebSocketContext* context = _Context;
        _Context = nullptr;
        if (context) {
            WebSocketContext::Release(context);
        }

        IWebSocketsServer* server = _Server;
        _Server = nullptr;
        if (server) {
            server->SetAccepter(nullptr);
            server->OnWebSocketRelease();
        }
        _Owner = nullptr;
        delete this;
    }
}
