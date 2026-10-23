#include "SshRoute.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <limits>
#include <utility>

namespace Foundation {
    SshRoute::SshRoute(
        SshConnectOptions options,
        fConnect connectCallback,
        fActivity activityCallback,
        fTimeout timeoutCallback,
        fDisconnected disconnectedCallback)
        : _Options(std::move(options)),
          _ConnectCallback(std::move(connectCallback)),
          _ActivityCallback(std::move(activityCallback)),
          _TimeoutCallback(std::move(timeoutCallback)),
          _DisconnectedCallback(std::move(disconnectedCallback)) {
        _Connections.reserve(_Options._JumpHosts.size() + 1);
        try {
            for (SshEndpointOptions& endpoint : _Options._JumpHosts) {
                auto connection = new Connection();
                _Connections.push_back(connection);
                connection->_Route = this;
                connection->_Index = _Connections.size() - 1;
                connection->_Endpoint = std::move(endpoint);
            }

            auto target = new Connection();
            _Connections.push_back(target);
            target->_Route = this;
            target->_Index = _Connections.size() - 1;
            target->_Endpoint = std::move(_Options._Target);
            _Options._JumpHosts.clear();
        }
        catch (...) {
            for (Connection* connection : _Connections) {
                delete connection;
            }
            throw;
        }
    }

    SshRoute::~SshRoute() {
        StopTimers();
        CleanupConnections();
        for (Connection* connection : _Connections) {
            delete connection;
        }
        _Connections.clear();
        _TimerApi = nullptr;
    }

    bool SshRoute::Start(INetApi* const netApi, ITimerApi* const timerApi) {
        if (_State != eRouteState::Created
            || !netApi
            || !timerApi
            || _Connections.empty()) {
            return false;
        }

        _TimerApi = timerApi;
        _State = eRouteState::TcpConnecting;
        StartConnectTimeoutTimer();

        const SshEndpointOptions& first = _Connections.front()->_Endpoint;
        const bool launched = netApi->LaunchTcpSession(
            this,
            first._Host,
            first._Port,
            s_TcpInitialBufferSize);
        if (!launched && !_ConnectCallbackCalled) {
            Fail(
                eSshConnectError::TcpConnectFailed,
                "Failed to launch TCP transport for " + EndpointLabel(0) + ".",
                false);
            _CanDestroy = true;
        }
        return launched;
    }

    LIBSSH2_SESSION* SshRoute::TargetSession() const {
        if (_State != eRouteState::Connected || _Connections.empty()) {
            return nullptr;
        }
        return _Connections.back()->_Session;
    }

    void SshRoute::CompleteConnectionSetup() {
        if (_State != eRouteState::Connected) {
            return;
        }
        StopConnectTimeoutTimer();
        _TimeoutCallback = nullptr;
    }

    void SshRoute::HandleWouldBlock() {
        bool needsOutboundRetry = false;
        for (const Connection* connection : _Connections) {
            if (!connection || !connection->_Session) {
                continue;
            }
            const int directions = libssh2_session_block_directions(connection->_Session);
            if (directions == 0
                || (directions & LIBSSH2_SESSION_BLOCK_OUTBOUND) != 0) {
                needsOutboundRetry = true;
                break;
            }
        }

        if (needsOutboundRetry) {
            StartRetryTimer();
        }
        else {
            StopRetryTimer();
        }
    }

    void SshRoute::NotifyProgress() {
        StopRetryTimer();
    }

    void SshRoute::RequestActivity() {
        if (_State == eRouteState::Connected) {
            StartRetryTimer();
        }
    }

    void SshRoute::Close() {
        StopTimers();
        if (_State == eRouteState::Closed || _State == eRouteState::Failed) {
            if (!_Pipe) {
                _CanDestroy = true;
            }
            return;
        }

        _State = eRouteState::Closing;
        CompleteConnect(
            false,
            eSshConnectError::Cancelled,
            "SSH route was cancelled.");
        CleanupConnections();
        if (_Pipe) {
            ITcpSession::Close();
        }
        else {
            _State = eRouteState::Closed;
            _CanDestroy = true;
        }
    }

    bool SshRoute::CanDestroy() const {
        return _CanDestroy;
    }

    int SshRoute::OnReceive(const void* const content, const int size) {
        if (!content || size <= 0) {
            return 0;
        }

        const char* const bytes = static_cast<const char*>(content);
        _ReceiveBuffer.insert(_ReceiveBuffer.end(), bytes, bytes + size);
        StopRetryTimer();
        if (_State == eRouteState::Connecting) {
            Drive();
        }
        else if (_State == eRouteState::Connected && _ActivityCallback) {
            try {
                _ActivityCallback();
            }
            catch (...) {
            }
        }
        return size;
    }

    void SshRoute::OnConnected() {
        if (_State != eRouteState::TcpConnecting) {
            return;
        }

        _State = eRouteState::Connecting;
        _CurrentConnectionIndex = 0;
        if (!InitializeConnection(*_Connections.front())) {
            Fail(
                eSshConnectError::InternalError,
                "Failed to initialize SSH protocol for " + EndpointLabel(0) + ".",
                true);
            return;
        }
        Drive();
    }

    void SshRoute::OnConnectFailed() {
        if (_State == eRouteState::Closing
            || _State == eRouteState::Closed
            || _State == eRouteState::Failed) {
            _CanDestroy = true;
            return;
        }

        Fail(
            eSshConnectError::TcpConnectFailed,
            "TCP connection failed for " + EndpointLabel(0) + ".",
            false);
        _CanDestroy = true;
    }

    void SshRoute::OnDisconnected() {
        const bool notifyDisconnected = _WasConnected && !_DisconnectedCallbackCalled;
        StopTimers();
        if (_State != eRouteState::Failed) {
            _State = eRouteState::Closed;
        }
        CompleteConnect(
            false,
            eSshConnectError::TcpConnectFailed,
            "SSH route transport was closed.");

        if (notifyDisconnected) {
            _DisconnectedCallbackCalled = true;
            if (_DisconnectedCallback) {
                try {
                    _DisconnectedCallback();
                }
                catch (...) {
                }
            }
        }

        CleanupConnections();
        _CanDestroy = true;
    }

    void SshRoute::OnTimer(
        const int id,
        const UInt64 context,
        const Int64 timestamp) {
        (void)context;
        (void)timestamp;
        switch (id) {
        case eRouteTimerId::ConnectTimeout:
            if (_State == eRouteState::TcpConnecting
                || _State == eRouteState::Connecting) {
                Fail(
                    eSshConnectError::Timeout,
                    "SSH route connection timed out at "
                        + EndpointLabel(_CurrentConnectionIndex) + ".",
                    true);
            }
            else if (_State == eRouteState::Connected) {
                const fTimeout callback = std::move(_TimeoutCallback);
                _TimeoutCallback = nullptr;
                if (callback) {
                    try {
                        callback();
                    }
                    catch (...) {
                    }
                }
            }
            break;
        case eRouteTimerId::Retry:
            if (_State == eRouteState::Connecting) {
                Drive();
            }
            else if (_State == eRouteState::Connected && _ActivityCallback) {
                try {
                    _ActivityCallback();
                }
                catch (...) {
                }
            }
            break;
        default:
            break;
        }
    }

    ssize_t SshRoute::ReceiveTransport(
        libssh2_socket_t socket,
        void* const buffer,
        const std::size_t length,
        const int flags,
        void** const abstract) {
        (void)socket;
        (void)flags;
        if (!abstract || !*abstract || !buffer || length == 0) {
            return -1;
        }
        Connection* const connection = static_cast<Connection*>(*abstract);
        return connection->_Route->Receive(*connection, buffer, length);
    }

    ssize_t SshRoute::SendTransport(
        libssh2_socket_t socket,
        const void* const buffer,
        const std::size_t length,
        const int flags,
        void** const abstract) {
        (void)socket;
        (void)flags;
        if (!abstract || !*abstract || !buffer || length == 0) {
            return -1;
        }
        Connection* const connection = static_cast<Connection*>(*abstract);
        return connection->_Route->Send(*connection, buffer, length);
    }

    bool SshRoute::InitializeConnection(Connection& connection) {
        if (connection._Session) {
            return false;
        }

        connection._Session = libssh2_session_init_ex(
            nullptr,
            nullptr,
            nullptr,
            &connection);
        if (!connection._Session) {
            return false;
        }
        libssh2_session_callback_set2(
            connection._Session,
            LIBSSH2_CALLBACK_RECV,
            reinterpret_cast<libssh2_cb_generic*>(&SshRoute::ReceiveTransport));
        libssh2_session_callback_set2(
            connection._Session,
            LIBSSH2_CALLBACK_SEND,
            reinterpret_cast<libssh2_cb_generic*>(&SshRoute::SendTransport));
        libssh2_session_set_blocking(connection._Session, 0);
        connection._State = eConnectionState::Handshaking;
        return true;
    }

    void SshRoute::Drive() {
        if (_Driving) {
            _DrivePending = true;
            return;
        }
        if (_State != eRouteState::Connecting
            || _CurrentConnectionIndex >= _Connections.size()) {
            return;
        }

        do {
            _DrivePending = false;
            _Driving = true;
            bool continueDriving = true;

            for (int iteration = 0;
                iteration < 32
                    && continueDriving
                    && _State == eRouteState::Connecting;
                ++iteration) {
                Connection& connection = *_Connections[_CurrentConnectionIndex];
                LIBSSH2_SESSION* const session = connection._Session;
                if (!session) {
                    Fail(
                        eSshConnectError::InternalError,
                        "SSH protocol state is unavailable for "
                            + EndpointLabel(_CurrentConnectionIndex) + ".",
                        true);
                    break;
                }

                switch (connection._State) {
                case eConnectionState::Handshaking: {
                    _TransportFailed = false;
                    const int result = libssh2_session_handshake(session, 0);
                    if (result == LIBSSH2_ERROR_EAGAIN) {
                        HandleWouldBlock();
                        continueDriving = false;
                    }
                    else if (result != 0 || _TransportFailed) {
                        Fail(
                            _CurrentConnectionIndex == 0
                                ? eSshConnectError::HandshakeFailed
                                : eSshConnectError::TunnelFailed,
                            GetSessionError(
                                connection,
                                "SSH handshake failed for "
                                    + EndpointLabel(_CurrentConnectionIndex) + "."),
                            true);
                        continueDriving = false;
                    }
                    else if (!VerifyHostKey(connection)) {
                        Fail(
                            eSshConnectError::HostKeyRejected,
                            "SSH host key was rejected for "
                                + EndpointLabel(_CurrentConnectionIndex) + ".",
                            true);
                        continueDriving = false;
                    }
                    else {
                        connection._State = eConnectionState::Authenticating;
                    }
                    break;
                }
                case eConnectionState::Authenticating: {
                    const SshEndpointOptions& endpoint = connection._Endpoint;
                    _TransportFailed = false;
                    int result = 0;
                    if (endpoint._AuthType == eSshAuthType::Password) {
                        result = libssh2_userauth_password_ex(
                            session,
                            endpoint._UserName.c_str(),
                            static_cast<unsigned int>(endpoint._UserName.size()),
                            endpoint._Password.c_str(),
                            static_cast<unsigned int>(endpoint._Password.size()),
                            nullptr);
                    }
                    else {
                        const char* const publicKeyPath = endpoint._PublicKeyPath.empty()
                            ? nullptr
                            : endpoint._PublicKeyPath.c_str();
                        const char* const passphrase = endpoint._PrivateKeyPassphrase.empty()
                            ? nullptr
                            : endpoint._PrivateKeyPassphrase.c_str();
                        result = libssh2_userauth_publickey_fromfile_ex(
                            session,
                            endpoint._UserName.c_str(),
                            static_cast<unsigned int>(endpoint._UserName.size()),
                            publicKeyPath,
                            endpoint._PrivateKeyPath.c_str(),
                            passphrase);
                    }

                    if (result == LIBSSH2_ERROR_EAGAIN) {
                        HandleWouldBlock();
                        continueDriving = false;
                    }
                    else if (result != 0 || _TransportFailed) {
                        Fail(
                            _TransportFailed
                                ? eSshConnectError::TunnelFailed
                                : eSshConnectError::AuthenticationFailed,
                            GetSessionError(
                                connection,
                                "SSH authentication failed for "
                                    + EndpointLabel(_CurrentConnectionIndex) + "."),
                            true);
                        continueDriving = false;
                    }
                    else {
                        connection._Endpoint._Password.clear();
                        connection._Endpoint._PrivateKeyPassphrase.clear();
                        connection._State = eConnectionState::Authenticated;
                    }
                    break;
                }
                case eConnectionState::Authenticated:
                    if (_CurrentConnectionIndex + 1 == _Connections.size()) {
                        _State = eRouteState::Connected;
                        _WasConnected = true;
                        StopRetryTimer();
                        CompleteConnect(
                            true,
                            eSshConnectError::None,
                            std::string());
                        continueDriving = false;
                    }
                    else {
                        connection._State = eConnectionState::OpeningTunnel;
                    }
                    break;
                case eConnectionState::OpeningTunnel: {
                    const SshEndpointOptions& next =
                        _Connections[_CurrentConnectionIndex + 1]->_Endpoint;
                    _TransportFailed = false;
                    connection._TunnelToNext = libssh2_channel_direct_tcpip_ex(
                        session,
                        next._Host.c_str(),
                        static_cast<int>(next._Port),
                        "127.0.0.1",
                        0);
                    if (!connection._TunnelToNext) {
                        const int error = libssh2_session_last_errno(session);
                        if (error == LIBSSH2_ERROR_EAGAIN && !_TransportFailed) {
                            HandleWouldBlock();
                            continueDriving = false;
                        }
                        else {
                            Fail(
                                eSshConnectError::TunnelFailed,
                                GetSessionError(
                                    connection,
                                    "Failed to open SSH tunnel from "
                                        + EndpointLabel(_CurrentConnectionIndex)
                                        + " to "
                                        + EndpointLabel(_CurrentConnectionIndex + 1)
                                        + "."),
                                true);
                            continueDriving = false;
                        }
                    }
                    else {
                        ++_CurrentConnectionIndex;
                        if (!InitializeConnection(*_Connections[_CurrentConnectionIndex])) {
                            Fail(
                                eSshConnectError::InternalError,
                                "Failed to initialize tunneled SSH protocol for "
                                    + EndpointLabel(_CurrentConnectionIndex) + ".",
                                true);
                            continueDriving = false;
                        }
                    }
                    break;
                }
                case eConnectionState::Created:
                default:
                    Fail(
                        eSshConnectError::InternalError,
                        "SSH connection entered an invalid state at "
                            + EndpointLabel(_CurrentConnectionIndex) + ".",
                        true);
                    continueDriving = false;
                    break;
                }
            }

            _Driving = false;
        } while (_DrivePending && _State == eRouteState::Connecting);
    }

    bool SshRoute::VerifyHostKey(const Connection& connection) const {
        const SshEndpointOptions& endpoint = connection._Endpoint;
        if (endpoint._HostKeyPolicy == eSshHostKeyPolicy::Insecure) {
            return true;
        }

        std::size_t hostKeyLength = 0;
        int hostKeyType = LIBSSH2_HOSTKEY_TYPE_UNKNOWN;
        const char* const hostKey = libssh2_session_hostkey(
            connection._Session,
            &hostKeyLength,
            &hostKeyType);
        const int keyMask = GetKnownHostKeyMask(hostKeyType);
        if (!hostKey
            || hostKeyLength == 0
            || keyMask == LIBSSH2_KNOWNHOST_KEY_UNKNOWN) {
            return false;
        }

        LIBSSH2_KNOWNHOSTS* const knownHosts =
            libssh2_knownhost_init(connection._Session);
        if (!knownHosts) {
            return false;
        }

        const bool knownHostsExists = std::filesystem::exists(endpoint._KnownHostsPath);
        if (knownHostsExists
            && libssh2_knownhost_readfile(
                knownHosts,
                endpoint._KnownHostsPath.c_str(),
                LIBSSH2_KNOWNHOST_FILE_OPENSSH) < 0) {
            libssh2_knownhost_free(knownHosts);
            return false;
        }

        const int typeMask = LIBSSH2_KNOWNHOST_TYPE_PLAIN
            | LIBSSH2_KNOWNHOST_KEYENC_RAW
            | keyMask;
        const int checkResult = libssh2_knownhost_checkp(
            knownHosts,
            endpoint._Host.c_str(),
            endpoint._Port,
            hostKey,
            hostKeyLength,
            typeMask,
            nullptr);
        if (checkResult == LIBSSH2_KNOWNHOST_CHECK_MATCH) {
            libssh2_knownhost_free(knownHosts);
            return true;
        }
        if (checkResult != LIBSSH2_KNOWNHOST_CHECK_NOTFOUND
            || endpoint._HostKeyPolicy != eSshHostKeyPolicy::AcceptNew) {
            libssh2_knownhost_free(knownHosts);
            return false;
        }

        const std::filesystem::path knownHostsPath(endpoint._KnownHostsPath);
        const std::filesystem::path parentPath = knownHostsPath.parent_path();
        std::error_code fileError;
        if (!parentPath.empty()) {
            std::filesystem::create_directories(parentPath, fileError);
        }
        if (fileError) {
            libssh2_knownhost_free(knownHosts);
            return false;
        }

        const std::string knownHostName = endpoint._Port == 22
            ? endpoint._Host
            : "[" + endpoint._Host + "]:" + std::to_string(endpoint._Port);
        const int addResult = libssh2_knownhost_addc(
            knownHosts,
            knownHostName.c_str(),
            nullptr,
            hostKey,
            hostKeyLength,
            nullptr,
            0,
            typeMask,
            nullptr);
        const int writeResult = addResult == 0
            ? libssh2_knownhost_writefile(
                knownHosts,
                endpoint._KnownHostsPath.c_str(),
                LIBSSH2_KNOWNHOST_FILE_OPENSSH)
            : -1;
        libssh2_knownhost_free(knownHosts);
        return addResult == 0 && writeResult == 0;
    }

    int SshRoute::GetKnownHostKeyMask(const int hostKeyType) const {
        switch (hostKeyType) {
        case LIBSSH2_HOSTKEY_TYPE_RSA:
            return LIBSSH2_KNOWNHOST_KEY_SSHRSA;
        case LIBSSH2_HOSTKEY_TYPE_DSS:
            return LIBSSH2_KNOWNHOST_KEY_SSHDSS;
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_256:
            return LIBSSH2_KNOWNHOST_KEY_ECDSA_256;
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_384:
            return LIBSSH2_KNOWNHOST_KEY_ECDSA_384;
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_521:
            return LIBSSH2_KNOWNHOST_KEY_ECDSA_521;
        case LIBSSH2_HOSTKEY_TYPE_ED25519:
            return LIBSSH2_KNOWNHOST_KEY_ED25519;
        default:
            return LIBSSH2_KNOWNHOST_KEY_UNKNOWN;
        }
    }

    ssize_t SshRoute::Receive(
        Connection& connection,
        void* const buffer,
        const std::size_t length) {
        if (connection._Index == 0) {
            const std::size_t available = _ReceiveBuffer.size() - _ReceiveOffset;
            if (available == 0) {
                return -EAGAIN;
            }
            const std::size_t copySize = (std::min)(available, length);
            std::memcpy(
                buffer,
                _ReceiveBuffer.data() + _ReceiveOffset,
                copySize);
            _ReceiveOffset += copySize;
            if (_ReceiveOffset == _ReceiveBuffer.size()) {
                _ReceiveBuffer.clear();
                _ReceiveOffset = 0;
            }
            else if (_ReceiveOffset >= 64 * 1024
                && _ReceiveOffset * 2 >= _ReceiveBuffer.size()) {
                _ReceiveBuffer.erase(
                    _ReceiveBuffer.begin(),
                    _ReceiveBuffer.begin()
                        + static_cast<std::ptrdiff_t>(_ReceiveOffset));
                _ReceiveOffset = 0;
            }
            return static_cast<ssize_t>(copySize);
        }

        Connection& previous = *_Connections[connection._Index - 1];
        if (!previous._TunnelToNext) {
            _TransportFailed = true;
            return -1;
        }
        const ssize_t result = libssh2_channel_read_ex(
            previous._TunnelToNext,
            0,
            static_cast<char*>(buffer),
            length);
        if (result == LIBSSH2_ERROR_EAGAIN || result == 0) {
            if (result == 0 && libssh2_channel_eof(previous._TunnelToNext)) {
                _TransportFailed = true;
                return 0;
            }
            return -EAGAIN;
        }
        if (result < 0) {
            _TransportFailed = true;
            return -1;
        }
        return result;
    }

    ssize_t SshRoute::Send(
        Connection& connection,
        const void* const buffer,
        const std::size_t length) {
        if (connection._Index == 0) {
            if (!_Pipe) {
                _TransportFailed = true;
                return -1;
            }
            const std::size_t writeSize = (std::min)(
                length,
                static_cast<std::size_t>((std::numeric_limits<int>::max)()));
            ITcpSession::Send(buffer, static_cast<int>(writeSize), true);
            return static_cast<ssize_t>(writeSize);
        }

        Connection& previous = *_Connections[connection._Index - 1];
        if (!previous._TunnelToNext) {
            _TransportFailed = true;
            return -1;
        }
        const ssize_t result = libssh2_channel_write_ex(
            previous._TunnelToNext,
            0,
            static_cast<const char*>(buffer),
            length);
        if (result == LIBSSH2_ERROR_EAGAIN) {
            return -EAGAIN;
        }
        if (result < 0) {
            _TransportFailed = true;
            return -1;
        }
        return result;
    }

    void SshRoute::StartConnectTimeoutTimer() {
        if (!_TimerApi || _TimerApi->IsExistsTimer(this, eRouteTimerId::ConnectTimeout)) {
            return;
        }
        const std::uint32_t maximumDelay = static_cast<std::uint32_t>(
            (std::numeric_limits<int>::max)());
        const std::uint32_t requestedDelay = (std::min)(
            _Options._TimeoutMilliseconds,
            maximumDelay);
        const int delay = (std::max)(
            static_cast<int>(requestedDelay),
            s_TimerIntervalMilliseconds);
        _TimerApi->StartTimer(
            this,
            eRouteTimerId::ConnectTimeout,
            delay,
            1,
            s_TimerIntervalMilliseconds,
            0,
            __FILE__,
            __LINE__);
    }

    void SshRoute::StopConnectTimeoutTimer() {
        if (_TimerApi && _TimerApi->IsExistsTimer(this, eRouteTimerId::ConnectTimeout)) {
            _TimerApi->KillTimer(
                this,
                eRouteTimerId::ConnectTimeout,
                0,
                __FILE__,
                __LINE__);
        }
    }

    void SshRoute::StartRetryTimer() {
        if (!_TimerApi || _TimerApi->IsExistsTimer(this, eRouteTimerId::Retry)) {
            return;
        }
        _TimerApi->StartTimer(
            this,
            eRouteTimerId::Retry,
            s_TimerIntervalMilliseconds,
            Unlimited,
            s_TimerIntervalMilliseconds,
            0,
            __FILE__,
            __LINE__);
    }

    void SshRoute::StopRetryTimer() {
        if (_TimerApi && _TimerApi->IsExistsTimer(this, eRouteTimerId::Retry)) {
            _TimerApi->KillTimer(
                this,
                eRouteTimerId::Retry,
                0,
                __FILE__,
                __LINE__);
        }
    }

    void SshRoute::StopTimers() {
        StopConnectTimeoutTimer();
        StopRetryTimer();
    }

    void SshRoute::CompleteConnect(
        const bool succeed,
        const eSshConnectError error,
        const std::string& message) {
        if (_ConnectCallbackCalled) {
            return;
        }
        _ConnectCallbackCalled = true;
        const fConnect callback = std::move(_ConnectCallback);
        _ConnectCallback = nullptr;
        if (!callback) {
            return;
        }
        try {
            callback(succeed, error, message);
        }
        catch (...) {
        }
    }

    void SshRoute::Fail(
        const eSshConnectError error,
        const std::string& message,
        const bool closeTcp) {
        if (_State == eRouteState::Failed || _State == eRouteState::Closed) {
            return;
        }
        _State = eRouteState::Failed;
        StopTimers();
        CompleteConnect(false, error, message);
        CleanupConnections();
        if (closeTcp && _Pipe) {
            ITcpSession::Close();
        }
        else if (!_Pipe) {
            _CanDestroy = true;
        }
    }

    void SshRoute::CleanupConnections() {
        for (auto iterator = _Connections.rbegin(); iterator != _Connections.rend(); ++iterator) {
            Connection* const connection = *iterator;
            if (!connection) {
                continue;
            }

            LIBSSH2_CHANNEL* const tunnel = connection->_TunnelToNext;
            LIBSSH2_SESSION* const session = connection->_Session;
            connection->_TunnelToNext = nullptr;
            connection->_Session = nullptr;

            if (tunnel) {
                (void)libssh2_channel_free(tunnel);
            }
            if (session) {
                (void)libssh2_session_disconnect_ex(
                    session,
                    SSH_DISCONNECT_BY_APPLICATION,
                    "Foundation SSH route closed",
                    "");
                (void)libssh2_session_free(session);
            }
        }
    }

    std::string SshRoute::GetSessionError(
        const Connection& connection,
        const std::string& fallback) const {
        if (!connection._Session) {
            return fallback;
        }
        char* errorMessage = nullptr;
        int errorLength = 0;
        libssh2_session_last_error(
            connection._Session,
            &errorMessage,
            &errorLength,
            0);
        if (!errorMessage || errorLength <= 0) {
            return fallback;
        }
        return EndpointLabel(connection._Index) + ": "
            + std::string(errorMessage, static_cast<std::size_t>(errorLength));
    }

    std::string SshRoute::EndpointLabel(const std::size_t index) const {
        if (index >= _Connections.size() || !_Connections[index]) {
            return "SSH route endpoint";
        }
        const SshEndpointOptions& endpoint = _Connections[index]->_Endpoint;
        const std::string role = index + 1 == _Connections.size()
            ? "target"
            : "jump " + std::to_string(index);
        return role + " (" + endpoint._Host + ":"
            + std::to_string(endpoint._Port) + ")";
    }
}
