#include "Ssh.h"

#include "SftpSession.h"
#include "SshSession.h"
#include "SshTcpTunnel.h"
#include "FoundationApi.h"

#include <libssh2.h>

#include <new>

namespace Foundation {
    Ssh::~Ssh() {
        Release(nullptr);
    }

    bool Ssh::Initialize(FoundationApi * const foundation) {
        if (_Initialized
            || !foundation
            || !foundation->NetApi()
            || !foundation->TimerApi()) {
            return false;
        }

        if (libssh2_init(0) != 0) {
            return false;
        }

        _NetApi = foundation->NetApi();
        _TimerApi = foundation->TimerApi();
        _Initialized = true;
        return true;
    }

    bool Ssh::Launch(FoundationApi * const foundation) {
        (void)foundation;
        if (!_Initialized || _Launched) {
            return false;
        }

        _Launched = true;
        return true;
    }

    void Ssh::Release(FoundationApi * const foundation) {
        (void)foundation;
        if (!_Initialized) {
            return;
        }

        for (SshSession * session : _Sessions) {
            session->Close();
        }
        for (SftpSession* session : _SftpSessions) {
            session->Close();
        }
        for (SshTcpTunnel* const tunnel : _TcpTunnels) {
            tunnel->Close();
        }

        for (SshSession * session : _Sessions) {
            delete session;
        }
        _Sessions.clear();
        for (SftpSession* session : _SftpSessions) {
            delete session;
        }
        _SftpSessions.clear();
        for (SshTcpTunnel* const tunnel : _TcpTunnels) {
            delete tunnel;
        }
        _TcpTunnels.clear();

        libssh2_exit();
        _NetApi = nullptr;
        _TimerApi = nullptr;
        _Launched = false;
        _Initialized = false;
    }

    void Ssh::EarlyUpdate(FoundationApi * const foundation) {
        (void)foundation;
    }

    void Ssh::Update(FoundationApi * const foundation) {
        (void)foundation;
        if (!_Launched) {
            return;
        }

        DeleteReleasedSessions();
    }

    void Ssh::LaterUpdate(FoundationApi * const foundation) {
        (void)foundation;
    }

    void Ssh::Connect(
        const SshConnectOptions & options,
        const fSshConnect & callback) {
        std::string errorMessage;
        if (!_Launched || !_NetApi || !_TimerApi || !ValidateOptions(options, errorMessage)) {
            if (callback) {
                callback(false, nullptr, eSshConnectError::InvalidOptions, errorMessage);
            }
            return;
        }

        SshSession * session = new (std::nothrow) SshSession(options, callback);
        if (!session) {
            if (callback) {
                callback(false, nullptr, eSshConnectError::InternalError, "Failed to allocate SSH session.");
            }
            return;
        }

        _Sessions.push_back(session);
        session->Start(_NetApi, _TimerApi);
    }

    void Ssh::ConnectSftp(
        const SshConnectOptions& options,
        const fSftpConnect& callback) {
        std::string errorMessage;
        if (!_Launched
            || !_NetApi
            || !_TimerApi
            || !ValidateOptions(options, errorMessage)) {
            if (callback) {
                callback(
                    false,
                    nullptr,
                    eSshConnectError::InvalidOptions,
                    errorMessage);
            }
            return;
        }

        SftpSession* session =
            new (std::nothrow) SftpSession(options, callback);
        if (!session) {
            if (callback) {
                callback(
                    false,
                    nullptr,
                    eSshConnectError::InternalError,
                    "Failed to allocate SFTP session.");
            }
            return;
        }

        _SftpSessions.push_back(session);
        session->Start(_NetApi, _TimerApi);
    }

    bool Ssh::LaunchTcpTunnel(
        ITcpSession* const session,
        const SshTcpTunnelOptions& options) {
        std::string errorMessage;
        if (!_Launched
            || !_NetApi
            || !_TimerApi
            || !session
            || session->_Pipe
            || options._DestinationHost.empty()
            || options._DestinationPort == 0
            || options._ReceiveBufferSize <= 0
            || !ValidateOptions(options._Connection, errorMessage)) {
            return false;
        }

        SshTcpTunnel* const tunnel = new (std::nothrow) SshTcpTunnel(
            options,
            session);
        if (!tunnel) {
            return false;
        }
        _TcpTunnels.push_back(tunnel);
        return tunnel->Start(_NetApi, _TimerApi);
    }

    bool Ssh::ValidateOptions(
        const SshConnectOptions & options,
        std::string & errorMessage) const {
        static constexpr std::size_t maximumJumpHostCount = 8;
        if (options._JumpHosts.size() > maximumJumpHostCount) {
            errorMessage = "SSH route exceeds the maximum jump-host count.";
            return false;
        }
        if (options._TimeoutMilliseconds == 0) {
            errorMessage = "SSH timeout must be greater than zero.";
            return false;
        }

        for (std::size_t index = 0; index < options._JumpHosts.size(); ++index) {
            if (!ValidateEndpoint(
                options._JumpHosts[index],
                "SSH jump " + std::to_string(index),
                errorMessage)) {
                return false;
            }
        }
        if (!ValidateEndpoint(options._Target, "SSH target", errorMessage)) {
            return false;
        }

        errorMessage.clear();
        return true;
    }

    bool Ssh::ValidateEndpoint(
        const SshEndpointOptions& endpoint,
        const std::string& label,
        std::string& errorMessage) const {
        if (endpoint._Host.empty()) {
            errorMessage = label + " host is empty.";
            return false;
        }
        if (endpoint._Port == 0) {
            errorMessage = label + " port must be greater than zero.";
            return false;
        }
        if (endpoint._UserName.empty()) {
            errorMessage = label + " user name is empty.";
            return false;
        }
        if (endpoint._AuthType == eSshAuthType::Password
            && endpoint._Password.empty()) {
            errorMessage = label + " password is empty.";
            return false;
        }
        if (endpoint._AuthType == eSshAuthType::PublicKey
            && endpoint._PrivateKeyPath.empty()) {
            errorMessage = label + " private key path is empty.";
            return false;
        }
        if (endpoint._HostKeyPolicy != eSshHostKeyPolicy::Insecure
            && endpoint._KnownHostsPath.empty()) {
            errorMessage = label
                + " requires a known-hosts path for the selected host-key policy.";
            return false;
        }
        return true;
    }

    void Ssh::DeleteReleasedSessions() {
        for (auto iterator = _Sessions.begin(); iterator != _Sessions.end();) {
            SshSession * session = *iterator;
            if (!session->CanDestroy()) {
                ++iterator;
                continue;
            }

            delete session;
            iterator = _Sessions.erase(iterator);
        }

        for (auto iterator = _SftpSessions.begin();
            iterator != _SftpSessions.end();) {
            SftpSession* session = *iterator;
            if (!session->CanDestroy()) {
                ++iterator;
                continue;
            }

            delete session;
            iterator = _SftpSessions.erase(iterator);
        }

        for (auto iterator = _TcpTunnels.begin();
            iterator != _TcpTunnels.end();) {
            SshTcpTunnel* const tunnel = *iterator;
            if (!tunnel->CanDestroy()) {
                ++iterator;
                continue;
            }

            delete tunnel;
            iterator = _TcpTunnels.erase(iterator);
        }
    }
}
