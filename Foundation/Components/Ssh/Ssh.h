#ifndef SSH_H
#define SSH_H

#include "Header.h"

namespace Foundation {
    class INetApi;
    class ITimerApi;
    class SftpSession;
    class SshSession;
    class SshTcpTunnel;

    class Ssh final : public ISsh {
    public:
        Ssh() = default;
        ~Ssh() override;

        bool Initialize(FoundationApi* const foundation) override;
        bool Launch(FoundationApi* const foundation) override;
        void Release(FoundationApi* const foundation) override;

        void EarlyUpdate(FoundationApi* const foundation) override;
        void Update(FoundationApi* const foundation) override;
        void LaterUpdate(FoundationApi* const foundation) override;

        void Connect(
            const SshConnectOptions& options,
            const fSshConnect& callback) override;
        void ConnectSftp(
            const SshConnectOptions& options,
            const fSftpConnect& callback) override;
        bool LaunchTcpTunnel(
            ITcpSession* session,
            const SshTcpTunnelOptions& options) override;

    private:
        bool ValidateOptions(const SshConnectOptions& options, std::string& errorMessage) const;
        bool ValidateEndpoint(
            const SshEndpointOptions& endpoint,
            const std::string& label,
            std::string& errorMessage) const;
        void DeleteReleasedSessions();

        INetApi* _NetApi = nullptr;
        ITimerApi* _TimerApi = nullptr;
        std::vector<SshSession*> _Sessions;
        std::vector<SftpSession*> _SftpSessions;
        std::vector<SshTcpTunnel*> _TcpTunnels;
        bool _Initialized = false;
        bool _Launched = false;
    };
}

#endif // SSH_H
