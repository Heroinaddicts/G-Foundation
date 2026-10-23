#ifndef SSH_TCP_TUNNEL_H
#define SSH_TCP_TUNNEL_H

#include "Header.h"

namespace Foundation {
    class SshRoute;

    class SshTcpTunnel final : public ITcpPipe {
    public:
        SshTcpTunnel(
            SshTcpTunnelOptions options,
            ITcpSession* session);
        ~SshTcpTunnel() override;

        SshTcpTunnel(const SshTcpTunnel&) = delete;
        SshTcpTunnel& operator=(const SshTcpTunnel&) = delete;

        bool Start(INetApi* netApi, ITimerApi* timerApi);
        bool CanDestroy() const;

        void Pause() override;
        void Resume() override;
        void Close() override;
        void Send(const void* data, int size, bool immediately) override;
        std::string RemoteIp() const override;
        int RemotePort() const override;
        bool IsActiveConnect() const override;
        void DetachSession(ITcpSession* session) override;

    private:
        enum class eTunnelState : std::uint8_t {
            Created,
            ConnectingRoute,
            OpeningChannel,
            Connected,
            Closing,
            Closed,
            Failed
        };

        void OnRouteConnected(
            bool succeed,
            eSshConnectError error,
            const std::string& message);
        void OnRouteDisconnected();
        void Drive();
        bool FlushWrites();
        bool DeliverReceivedData();
        bool ReadChannel();
        void FailConnect();
        void Disconnect(bool closeRoute, bool notifySession);
        void CleanupChannel();
        ITcpSession* DetachBoundSession();
        SshTcpTunnelOptions _Options;
        ITcpSession* _Session = nullptr;
        SshRoute* _Route = nullptr;
        LIBSSH2_CHANNEL* _Channel = nullptr;
        std::vector<char> _SendBuffer;
        std::size_t _SendOffset = 0;
        std::vector<char> _ReceiveBuffer;
        std::size_t _ReceiveOffset = 0;
        eTunnelState _State = eTunnelState::Created;
        bool _Paused = false;
        bool _Driving = false;
        bool _DrivePending = false;
    };
}

#endif // SSH_TCP_TUNNEL_H
