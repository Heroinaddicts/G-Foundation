#ifndef SSH_API_H
#define SSH_API_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Foundation {
    class ITcpSession;

    enum class eSftpTransferError : std::uint8_t {
        None,
        OpenFailed,
        WriteFailed,
        CloseFailed,
        Cancelled,
        ConnectionClosed
    };

    struct SftpUploadRequest {
        std::string _RemotePath;
        std::vector<std::uint8_t> _Content;
        std::uint32_t _Permissions = 0644;
    };

    class ISftpSession {
    public:
        enum class eSftpSessionEvent : std::uint8_t {
            Connected,
            Disconnected
        };

        using fSftpSessionEvent = std::function<void(ISftpSession* const)>;
        using fSftpUploadProgress = std::function<void(
            ISftpSession* const,
            std::uint64_t,
            std::uint64_t)>;
        using fSftpUploadComplete = std::function<void(
            ISftpSession* const,
            bool,
            eSftpTransferError,
            std::uint64_t,
            const std::string&)>;

        virtual ~ISftpSession() {}

        // Session and transfer callbacks run on the thread that drives
        // Foundation::Update. The provider owns the session; callers
        // borrow it until Disconnected and must never delete it.
        virtual void RegisterEvent(
            eSftpSessionEvent event,
            const fSftpSessionEvent& callback) = 0;

        // The request is moved into the session. At most one upload may be
        // active. A true return value means the asynchronous upload was
        // accepted; completion is reported exactly once through callback.
        virtual bool Upload(
            SftpUploadRequest request,
            const fSftpUploadComplete& callback,
            const fSftpUploadProgress& progressCallback = {}) = 0;

        virtual void Close() = 0;
    };

    enum class eSshAuthType : std::uint8_t {
        Password,
        PublicKey
    };

    enum class eSshHostKeyPolicy : std::uint8_t {
        Strict,
        AcceptNew,
        Insecure
    };

    enum class eSshConnectError : std::uint8_t {
        None,
        InvalidOptions,
        TcpConnectFailed,
        HandshakeFailed,
        HostKeyRejected,
        AuthenticationFailed,
        TunnelFailed,
        ChannelFailed,
        Cancelled,
        InternalError,
        Timeout
    };

    struct SshEndpointOptions {
        std::string _Host;
        std::uint16_t _Port = 22;
        std::string _UserName;
        eSshAuthType _AuthType = eSshAuthType::Password;
        std::string _Password;
        std::string _PublicKeyPath;
        std::string _PrivateKeyPath;
        std::string _PrivateKeyPassphrase;
        eSshHostKeyPolicy _HostKeyPolicy = eSshHostKeyPolicy::Strict;
        std::string _KnownHostsPath;
    };

    struct SshConnectOptions {
        // The target is the final authenticated SSH endpoint. Shell and SFTP
        // sessions are opened there; TCP tunnels open their final direct-tcpip
        // channel there. Jump hosts are ordered from the client-facing host to
        // the target-facing host. An empty list means direct connect.
        SshEndpointOptions _Target;
        std::vector<SshEndpointOptions> _JumpHosts;

        // One deadline covers TCP connect, every jump handshake and
        // authentication, inter-hop tunnel creation, and the final session
        // setup: shell channel/PTY/shell request, SFTP subsystem initialization,
        // or a TCP tunnel's final direct-tcpip channel.
        std::uint32_t _TimeoutMilliseconds = 10000;
    };

    struct SshTcpTunnelOptions {
        // Establishes and authenticates the SSH connection chain first.
        SshConnectOptions _Connection;

        // Resolved by the final SSH server. Loopback is the safe default for
        // services that should not be exposed on the target network.
        std::string _DestinationHost = "127.0.0.1";
        std::uint16_t _DestinationPort = 0;

        // Maximum bytes retained while the consumer reports zero or partial
        // consumption. Reaching this limit without progress closes the tunnel.
        int _ReceiveBufferSize = 64 * 1024;
    };

    class ISshSession {
    public:
        enum class eSshSessionEvent : std::uint8_t {
            Connected,
            Disconnected,
            Receive
        };

        using fSshSessionEvent = std::function<void(
            ISshSession * const,
            const void * const,
            std::uint32_t)>;

        virtual ~ISshSession() {}

        virtual void RegisterEvent(
            eSshSessionEvent event,
            const fSshSessionEvent & callback) = 0;
        virtual bool Send(const void * data, std::uint32_t size) = 0;
        // Requests a PTY size change without waiting for the remote peer. The
        // implementation serializes the request with channel I/O.
        virtual bool ResizeTerminal(std::uint32_t columns, std::uint32_t rows) = 0;
        virtual void Close() = 0;
    };

    class ISshApi {
    public:
        using fSshConnect = std::function<void(
            bool,
            ISshSession * const,
            eSshConnectError,
            const std::string &)>;
        using fSftpConnect = std::function<void(
            bool,
            ISftpSession* const,
            eSshConnectError,
            const std::string&)>;

        virtual ~ISshApi() {}

        virtual void Connect(
            const SshConnectOptions & options,
            const fSshConnect & callback) = 0;
        virtual void ConnectSftp(
            const SshConnectOptions& options,
            const fSftpConnect& callback) = 0;

        // Binds session to a direct-tcpip channel opened by the final SSH
        // server after the complete jump-host chain is authenticated. The
        // caller owns session; Foundation owns the backing pipe until close.
        virtual bool LaunchTcpTunnel(
            ITcpSession* session,
            const SshTcpTunnelOptions& options) = 0;
    };
}

#endif // SSH_API_H
