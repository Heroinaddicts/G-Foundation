#ifndef WEB_SOCKETS_API_H
#define WEB_SOCKETS_API_H

#include "MultiSys.hpp"

namespace Foundation {
    class WebSocketPipe;
    class WebSockets;
    class WebSocketsAccept;

    enum class eWebSocketSessionEvent : uint32_t {
        Connectfaild,
        Connected,
        Disconnected,
        __COUNT__
    };

    enum class eWebSocketMessageType : uint8_t {
        Text,
        Binary
    };

    class IWebSocketPipe {
    public:
        virtual ~IWebSocketPipe() {}

        virtual void Send(const void* data, const int size, bool immediately) = 0;
        virtual const char* RemoteIp() const = 0;
        virtual int RemotePort() const = 0;
        virtual bool IsActiveConnect() const = 0;
        virtual void Close() = 0;
        virtual void Pause() = 0;
        virtual void Resume() = 0;
        virtual void SendMessage(
            const void* data,
            const int size,
            const eWebSocketMessageType type,
            const bool immediately) {
            static_cast<void>(type);
            Send(data, size, immediately);
        }
    };

    class IWebSocketSession {
    public:

        virtual ~IWebSocketSession() {
            for (int i = 0; i < static_cast<int>(eWebSocketSessionEvent::__COUNT__); i++) {
                if (_WebSocketSessionEventPool[i]) {
                    txdel _WebSocketSessionEventPool[i];
                    _WebSocketSessionEventPool[i] = nullptr;
                }
            }
        }

        IWebSocketSession() {
            for (int i = 0; i < static_cast<int>(eWebSocketSessionEvent::__COUNT__); i++) {
                _WebSocketSessionEventPool[i] = nullptr;
            }
        }

        IWebSocketPipe* WebSocketPipe() const {
            return _WebSocketPipe;
        }

    private:
        friend class ::Foundation::WebSocketPipe;

        __forceinline void SetWebSocketPipe(IWebSocketPipe* const pipe) {
            _WebSocketPipe = pipe;
        }

    public:
        virtual int OnReceive(const void* const content, const int size) = 0;

        void OnWebSocketSessionEvent(const eWebSocketSessionEvent ev) {
            const int eventIndex = static_cast<int>(ev);
            if (eventIndex < 0 || eventIndex >= static_cast<int>(eWebSocketSessionEvent::__COUNT__)) {
                return;
            }
            if (nullptr != _WebSocketSessionEventPool[eventIndex]) {
                for (std::size_t i = 0; i < _WebSocketSessionEventPool[eventIndex]->size(); i++) {
                    (*_WebSocketSessionEventPool[eventIndex])[i](this);
                }
            }

            switch (ev) {
            case eWebSocketSessionEvent::Connectfaild: {
                OnConnectFailed();
                break;
            }
            case eWebSocketSessionEvent::Connected: {
                OnConnected();
                break;
            }
            case eWebSocketSessionEvent::Disconnected: {
                OnDisconnected();
                break;
            }
            }
        }

        __forceinline void Close() const { if (_WebSocketPipe) { _WebSocketPipe->Close(); } }
        __forceinline void Send(const void* context, const int size, bool immediately = true) const { if (_WebSocketPipe) { _WebSocketPipe->Send(context, size, immediately); } }
        __forceinline void SendText(const void* context, const int size, bool immediately = true) const {
            if (_WebSocketPipe) {
                _WebSocketPipe->SendMessage(context, size, eWebSocketMessageType::Text, immediately);
            }
        }
        __forceinline void SendBinaryMessage(const void* context, const int size, bool immediately = true) const {
            if (_WebSocketPipe) {
                _WebSocketPipe->SendMessage(context, size, eWebSocketMessageType::Binary, immediately);
            }
        }
        __forceinline void Pause() { if (_WebSocketPipe) { _WebSocketPipe->Pause(); } }
        __forceinline void Resume() { if (_WebSocketPipe) { _WebSocketPipe->Resume(); } }

        __forceinline const char* RemoteIp() const {
            if (_WebSocketPipe) {
                return _WebSocketPipe->RemoteIp();
            }

            return nullptr;
        }

        __forceinline int RemotePort() const {
            if (_WebSocketPipe) {
                return _WebSocketPipe->RemotePort();
            }

            return -1;
        }

        __forceinline bool IsActiveConnect() const {
            if (_WebSocketPipe) {
                return _WebSocketPipe->IsActiveConnect();
            }

            return false;
        }

        template<typename T>
        void RegisterWebSocketSessionEventCallback(const eWebSocketSessionEvent ev, std::function<void(T* const)> function) {
            PrivateRegisterWebSocketSessionEventCallback(ev, [=](IWebSocketSession* const session) {
                function(static_cast<T* const>(session));
                }
            );
        }

    protected:
        virtual void OnConnected() {}
        virtual void OnConnectFailed() {}
        virtual void OnDisconnected() {}

    private:
        using fWebSocketSessionEventCallback = std::function<void(IWebSocketSession* const)>;
        virtual void PrivateRegisterWebSocketSessionEventCallback(const eWebSocketSessionEvent ev, const fWebSocketSessionEventCallback& function) {
            const int eventIndex = static_cast<int>(ev);
            if (eventIndex < 0 || eventIndex >= static_cast<int>(eWebSocketSessionEvent::__COUNT__)) {
                return;
            }
            if (nullptr == _WebSocketSessionEventPool[eventIndex]) {
                _WebSocketSessionEventPool[eventIndex] = txnew std::vector<fWebSocketSessionEventCallback>();
            }
            _WebSocketSessionEventPool[eventIndex]->push_back(function);
        }

        IWebSocketPipe* _WebSocketPipe = nullptr;
        std::vector<fWebSocketSessionEventCallback>* _WebSocketSessionEventPool[static_cast<int>(eWebSocketSessionEvent::__COUNT__)];
    };

    class IWebSocketServerAccept {
    public:
        virtual ~IWebSocketServerAccept() {}
        virtual void Release() = 0;
    };

    class IWebSocketsServer {
    public:
        virtual ~IWebSocketsServer() {}
        IWebSocketsServer() {}

        virtual IWebSocketSession* OnMallocWebSocketSession(const char* remoteIp, const int remotePort) = 0;
        virtual void OnWebSocketError(IWebSocketSession* session) = 0;
        virtual void OnWebSocketRelease() = 0;

        virtual void Release() { if (_Accepter) { _Accepter->Release(); } }

        IWebSocketServerAccept* Accepter() const {
            return _Accepter;
        }

    private:
        friend class ::Foundation::WebSocketsAccept;
        friend class ::Foundation::WebSockets;

        __forceinline void SetAccepter(IWebSocketServerAccept* const accepter) {
            _Accepter = accepter;
        }

        IWebSocketServerAccept* _Accepter = nullptr;
    };

    class IWebSocketsApi {
    public:
        virtual ~IWebSocketsApi() {}

        virtual bool LaunchSession(IWebSocketSession* session, const char* host, const int port, const char* ca, const char* cert, const char* privateKey, const char* subproto = "") = 0;
        virtual bool LaunchServer(IWebSocketsServer* server, const char* ip, const int port, const char* cert, const char* privateKey, const char* subproto = "") = 0;
    };
}

#endif // WEB_SOCKETS_API_H
