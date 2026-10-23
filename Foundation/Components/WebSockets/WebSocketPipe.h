#ifndef WEB_SOCKET_PIPE_H
#define WEB_SOCKET_PIPE_H

#include "Header.h"

namespace Foundation {
    class WebSocketPipe final : public IWebSocketPipe {
    public:
        ~WebSocketPipe() override;

        static WebSocketPipe* Create(lws* wsi, WebSocketContext* context, bool active);
        static void Release(WebSocketPipe* pipe);

        void SetSession(IWebSocketSession* session);
        IWebSocketSession* DetachSession();
        IWebSocketSession* Session() const;
        bool OnClose(bool trigger);
        bool IsClosed() const;
        bool IsPaused() const;
        int WriteNextMessage(lws* wsi);

        FoundationUtils::XBuffer _RecvBuffer;
        FoundationUtils::XBuffer _SendBuffer;

    private:
        friend class WebSocketContext;
        friend class WebSockets;
        WebSocketPipe(lws* wsi, WebSocketContext* context, bool active);

        void Send(const void* data, int size, bool immediately) override;
        void SendMessage(
            const void* data,
            int size,
            eWebSocketMessageType type,
            bool immediately) override;
        const char* RemoteIp() const override;
        int RemotePort() const override;
        bool IsActiveConnect() const override;
        void Close() override;
        void Pause() override;
        void Resume() override;

        lws* _Wsi = nullptr;
        WebSocketContext* _WsContext = nullptr;
        IWebSocketSession* _Session = nullptr;
        std::string _RemoteIp;
        int _RemotePort = 0;
        bool _IsActive = false;
        std::atomic_bool _IsClosed = false;
        std::atomic_bool _IsPause = false;
        mutable FoundationUtils::SpinLock _SessionLock;

        struct PendingMessage {
            std::vector<unsigned char> _Payload;
            std::size_t _Offset = 0;
            eWebSocketMessageType _Type = eWebSocketMessageType::Binary;
        };

        mutable std::mutex _MessageMutex;
        std::deque<PendingMessage> _PendingMessages;
    };
}

#endif // WEB_SOCKET_PIPE_H
