#ifndef SSH_ROUTE_H
#define SSH_ROUTE_H

#include "Header.h"

namespace Foundation {
    class SshRoute final : public ITcpSession, private ITimer {
    public:
        using fConnect = std::function<void(
            bool,
            eSshConnectError,
            const std::string&)>;
        using fActivity = std::function<void()>;
        using fTimeout = std::function<void()>;
        using fDisconnected = std::function<void()>;

        SshRoute(
            SshConnectOptions options,
            fConnect connectCallback,
            fActivity activityCallback,
            fTimeout timeoutCallback,
            fDisconnected disconnectedCallback);
        ~SshRoute() override;

        SshRoute(const SshRoute&) = delete;
        SshRoute& operator=(const SshRoute&) = delete;

        bool Start(INetApi* netApi, ITimerApi* timerApi);
        LIBSSH2_SESSION* TargetSession() const;
        void CompleteConnectionSetup();
        void HandleWouldBlock();
        void NotifyProgress();
        void RequestActivity();
        void Close();
        bool CanDestroy() const;

        int OnReceive(const void* const content, int size) override;

    protected:
        void OnConnected() override;
        void OnConnectFailed() override;
        void OnDisconnected() override;
        void OnTimer(int id, UInt64 context, Int64 timestamp) override;

    private:
        enum class eRouteState : std::uint8_t {
            Created,
            TcpConnecting,
            Connecting,
            Connected,
            Closing,
            Closed,
            Failed
        };

        enum class eConnectionState : std::uint8_t {
            Created,
            Handshaking,
            Authenticating,
            Authenticated,
            OpeningTunnel
        };

        struct Connection final {
            SshRoute* _Route = nullptr;
            std::size_t _Index = 0;
            SshEndpointOptions _Endpoint;
            LIBSSH2_SESSION* _Session = nullptr;
            LIBSSH2_CHANNEL* _TunnelToNext = nullptr;
            eConnectionState _State = eConnectionState::Created;
        };

        enum eRouteTimerId : int {
            ConnectTimeout = 1,
            Retry
        };

        static ssize_t ReceiveTransport(
            libssh2_socket_t socket,
            void* buffer,
            std::size_t length,
            int flags,
            void** abstract);
        static ssize_t SendTransport(
            libssh2_socket_t socket,
            const void* buffer,
            std::size_t length,
            int flags,
            void** abstract);

        bool InitializeConnection(Connection& connection);
        void Drive();
        bool VerifyHostKey(const Connection& connection) const;
        int GetKnownHostKeyMask(int hostKeyType) const;
        ssize_t Receive(Connection& connection, void* buffer, std::size_t length);
        ssize_t Send(Connection& connection, const void* buffer, std::size_t length);
        void StartConnectTimeoutTimer();
        void StopConnectTimeoutTimer();
        void StartRetryTimer();
        void StopRetryTimer();
        void StopTimers();
        void CompleteConnect(
            bool succeed,
            eSshConnectError error,
            const std::string& message);
        void Fail(
            eSshConnectError error,
            const std::string& message,
            bool closeTcp);
        void CleanupConnections();
        std::string GetSessionError(
            const Connection& connection,
            const std::string& fallback) const;
        std::string EndpointLabel(std::size_t index) const;

        static constexpr int s_TcpInitialBufferSize =
            FoundationUtils::SafeMemory::Mb * 4;
        static constexpr int s_TimerIntervalMilliseconds = 2;

        SshConnectOptions _Options;
        std::vector<Connection*> _Connections;
        fConnect _ConnectCallback;
        fActivity _ActivityCallback;
        fTimeout _TimeoutCallback;
        fDisconnected _DisconnectedCallback;
        std::vector<char> _ReceiveBuffer;
        std::size_t _ReceiveOffset = 0;
        std::size_t _CurrentConnectionIndex = 0;
        ITimerApi* _TimerApi = nullptr;
        eRouteState _State = eRouteState::Created;
        bool _ConnectCallbackCalled = false;
        bool _WasConnected = false;
        bool _DisconnectedCallbackCalled = false;
        bool _CanDestroy = false;
        bool _Driving = false;
        bool _DrivePending = false;
        bool _TransportFailed = false;
    };
}

#endif // SSH_ROUTE_H
