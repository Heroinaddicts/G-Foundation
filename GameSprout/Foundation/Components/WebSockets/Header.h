#ifndef WEB_SOCKETS_HEADER_H
#define WEB_SOCKETS_HEADER_H

#include "IWebSockets.h"
#include "SafeQueue.hpp"
#include "SafeSystem.hpp"
#include "SafeThread.hpp"
#include "XBuffer.hpp"
#include "XLock.hpp"
#include "XPool.hpp"

#include <libwebsockets.h>

#if defined(_WIN32) && defined(SendMessage)
#undef SendMessage
#endif

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace Foundation {
    enum class eWebSocketContextType {
        Server,
        Client
    };

    class WebSockets;
    class WebSocketPipe;
    class WebSocketsAccept;
    class WebSocketContext;

    enum class eWebSocketEvent {
        Accepted,
        Connected,
        ConnectFailed,
        Receive,
        Disconnected,
        Unknown
    };

    struct WbsEvent {
        eWebSocketEvent _Event = eWebSocketEvent::Unknown;
        WebSocketContext* _Context = nullptr;
        WebSocketPipe* _Pipe = nullptr;
        IWebSocketSession* _Session = nullptr;
    };

    enum class eWebSocketRequest {
        Send,
        Close,
        ReleasePipe,
        Unknown
    };

    struct WbsRequest {
        eWebSocketRequest _Request = eWebSocketRequest::Unknown;
        WebSocketPipe* _Pipe = nullptr;
    };

}

#endif // WEB_SOCKETS_HEADER_H
