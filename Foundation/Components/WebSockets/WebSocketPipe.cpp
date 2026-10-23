#include "WebSocketPipe.h"

#include "WebSocketContext.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace Foundation {
    WebSocketPipe::WebSocketPipe(lws* wsi, WebSocketContext* context, bool active)
        : _RecvBuffer(4096, 4096),
        _SendBuffer(4096, 4096),
        _Wsi(wsi),
        _WsContext(context),
        _IsActive(active) {
        if (_Wsi) {
            char remoteIp[128] = {};
            if (lws_get_peer_simple(_Wsi, remoteIp, sizeof(remoteIp)) != nullptr) {
                _RemoteIp = remoteIp;
            }
        }
    }

    WebSocketPipe::~WebSocketPipe() {
        DetachSession();
    }

    WebSocketPipe* WebSocketPipe::Create(lws* wsi, WebSocketContext* context, bool active) {
        if (!wsi || !context) {
            return nullptr;
        }
        return new (std::nothrow) WebSocketPipe(wsi, context, active);
    }

    void WebSocketPipe::Release(WebSocketPipe* pipe) {
        if (pipe) {
            delete pipe;
        }
    }

    void WebSocketPipe::SetSession(IWebSocketSession* session) {
        if (!session) {
            return;
        }
        AUTO_LOCK(_SessionLock);
        if (_Session) {
            return;
        }
        _Session = session;
        _Session->SetWebSocketPipe(this);
    }

    IWebSocketSession* WebSocketPipe::DetachSession() {
        AUTO_LOCK(_SessionLock);
        IWebSocketSession* session = _Session;
        if (session) {
            session->SetWebSocketPipe(nullptr);
            _Session = nullptr;
        }
        return session;
    }

    IWebSocketSession* WebSocketPipe::Session() const {
        AUTO_LOCK(_SessionLock);
        return _Session;
    }

    bool WebSocketPipe::OnClose(bool trigger) {
        bool expected = false;
        if (!_IsClosed.compare_exchange_strong(expected, true)) {
            return false;
        }

        if (trigger && _WsContext) {
            WbsRequest* request = new (std::nothrow) WbsRequest();
            if (!request) {
                return true;
            }
            request->_Pipe = this;
            request->_Request = eWebSocketRequest::Close;
            _WsContext->PushRequest(request);
        }
        return true;
    }

    bool WebSocketPipe::IsClosed() const {
        return _IsClosed.load(std::memory_order_acquire);
    }

    bool WebSocketPipe::IsPaused() const {
        return _IsPause.load(std::memory_order_acquire);
    }

    void WebSocketPipe::Send(const void* data, int size, bool immediately) {
        if (!data || size <= 0 || IsClosed()) {
            return;
        }

        _SendBuffer.Write(data, size, __FILE__, __LINE__);
        if (immediately && _WsContext) {
            WbsRequest* request = new (std::nothrow) WbsRequest();
            if (!request) {
                return;
            }
            request->_Pipe = this;
            request->_Request = eWebSocketRequest::Send;
            _WsContext->PushRequest(request);
        }
    }

    void WebSocketPipe::SendMessage(
        const void* data,
        int size,
        eWebSocketMessageType type,
        bool immediately) {
        if (!data || size <= 0 || IsClosed()) {
            return;
        }

        PendingMessage message;
        message._Payload.resize(static_cast<std::size_t>(size));
        std::memcpy(message._Payload.data(), data, static_cast<std::size_t>(size));
        message._Type = type;
        {
            std::lock_guard<std::mutex> lock(_MessageMutex);
            _PendingMessages.emplace_back(std::move(message));
        }

        if (immediately && _WsContext) {
            WbsRequest* request = new (std::nothrow) WbsRequest();
            if (!request) {
                return;
            }
            request->_Pipe = this;
            request->_Request = eWebSocketRequest::Send;
            _WsContext->PushRequest(request);
        }
    }

    int WebSocketPipe::WriteNextMessage(lws* wsi) {
        constexpr std::size_t MaximumFragmentBytes = 64U * 1024U;

        std::lock_guard<std::mutex> lock(_MessageMutex);
        if (!wsi || _PendingMessages.empty() || IsClosed()) {
            return 0;
        }

        PendingMessage& message = _PendingMessages.front();
        if (message._Offset >= message._Payload.size()) {
            _PendingMessages.pop_front();
            return 0;
        }

        const std::size_t remaining = message._Payload.size() - message._Offset;
        const std::size_t fragmentSize = std::min(remaining, MaximumFragmentBytes);
        std::vector<unsigned char> buffer(LWS_PRE + fragmentSize);
        std::memcpy(
            buffer.data() + LWS_PRE,
            message._Payload.data() + message._Offset,
            fragmentSize);

        lws_write_protocol protocol = LWS_WRITE_CONTINUATION;
        if (message._Offset == 0) {
            protocol = message._Type == eWebSocketMessageType::Text
                ? LWS_WRITE_TEXT
                : LWS_WRITE_BINARY;
        }
        if (fragmentSize < remaining) {
            protocol = static_cast<lws_write_protocol>(
                static_cast<int>(protocol) | static_cast<int>(LWS_WRITE_NO_FIN));
        }

        const int written = lws_write(
            wsi,
            buffer.data() + LWS_PRE,
            fragmentSize,
            protocol);
        if (written != static_cast<int>(fragmentSize)) {
            return -1;
        }

        message._Offset += fragmentSize;
        if (message._Offset == message._Payload.size()) {
            _PendingMessages.pop_front();
        }
        return 1;
    }

    const char* WebSocketPipe::RemoteIp() const {
        return _RemoteIp.c_str();
    }

    int WebSocketPipe::RemotePort() const {
        return _RemotePort;
    }

    bool WebSocketPipe::IsActiveConnect() const {
        return _IsActive;
    }

    void WebSocketPipe::Close() {
        OnClose(true);
    }

    void WebSocketPipe::Pause() {
        _IsPause.store(true, std::memory_order_release);
    }

    void WebSocketPipe::Resume() {
        if (IsClosed() || !_IsPause.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        WbsEvent* event = new (std::nothrow) WbsEvent();
        if (!event) {
            return;
        }
        event->_Context = _WsContext;
        event->_Pipe = this;
        event->_Event = eWebSocketEvent::Receive;
        if (_WsContext) {
            _WsContext->PushEvent(event);
        }
        else {
            delete event;
        }
    }
}
