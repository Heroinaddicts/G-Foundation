#ifndef SSH_SESSION_H
#define SSH_SESSION_H

#include "Header.h"

namespace Foundation {
    class SshRoute;

    class SshSession final : public ISshSession {
    public:
        SshSession(
            const SshConnectOptions& options,
            const ISshApi::fSshConnect& connectCallback);
        ~SshSession() override;

        SshSession(const SshSession&) = delete;
        SshSession& operator=(const SshSession&) = delete;

        bool Start(INetApi* netApi, ITimerApi* timerApi);
        bool CanDestroy() const;

        void RegisterEvent(
            eSshSessionEvent event,
            const fSshSessionEvent& callback) override;
        bool Send(const void* data, std::uint32_t size) override;
        bool ResizeTerminal(std::uint32_t columns, std::uint32_t rows) override;
        void Close() override;

    private:
        enum class eSshState {
            Created,
            OpeningChannel,
            RequestingPty,
            StartingShell,
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
        void OnConnectTimeout();
        void DriveStateMachine();
        bool FlushChannelWrites();
        bool ApplyTerminalResize();
        bool ReadChannelOutput();
        void NotifyEvent(eSshSessionEvent event, const void* data, std::uint32_t size);
        void CompleteConnect(
            bool success,
            eSshConnectError error,
            const std::string& message);
        void Fail(eSshConnectError error, const std::string& message);
        void CleanupChannel();
        std::string GetSessionError(const std::string& fallback) const;

        static constexpr std::size_t s_EventCount = 3;

        SshConnectOptions _Options;
        ISshApi::fSshConnect _ConnectCallback;
        std::array<std::vector<fSshSessionEvent>, s_EventCount> _Events;
        std::vector<char> _SendBuffer;
        std::size_t _SendOffset = 0;
        std::uint32_t _PendingTerminalColumns = 0;
        std::uint32_t _PendingTerminalRows = 0;
        SshRoute* _Route = nullptr;
        LIBSSH2_CHANNEL* _Channel = nullptr;
        eSshState _State = eSshState::Created;
        bool _ConnectCallbackCalled = false;
        bool _Connected = false;
        bool _DisconnectEventCalled = false;
        bool _CanDestroy = false;
        bool _Driving = false;
        bool _DrivePending = false;
    };
}

#endif // SSH_SESSION_H
