#include "SshSession.h"

#include "SshRoute.h"

#include <utility>

namespace Foundation {
    SshSession::SshSession(
        const SshConnectOptions& options,
        const ISshApi::fSshConnect& connectCallback)
        : _Options(options),
        _ConnectCallback(connectCallback) {
    }

    SshSession::~SshSession() {
        CleanupChannel();
        if (_Route) {
            _Route->Close();
            delete _Route;
            _Route = nullptr;
        }
    }

    bool SshSession::Start(INetApi* const netApi, ITimerApi* const timerApi) {
        if (_State != eSshState::Created || !netApi || !timerApi || _Route) {
            return false;
        }

        _Route = new SshRoute(
            std::move(_Options),
            [this](
                const bool succeed,
                const eSshConnectError error,
                const std::string& message) {
                OnRouteConnected(succeed, error, message);
            },
            [this]() {
                DriveStateMachine();
            },
            [this]() {
                OnConnectTimeout();
            },
            [this]() {
                OnRouteDisconnected();
            });
        if (!_Route
            || !_Route->Start(netApi, timerApi)) {
            OnRouteConnected(
                false,
                eSshConnectError::TcpConnectFailed,
                "Failed to start SSH route.");
            return false;
        }
        return true;
    }

    bool SshSession::CanDestroy() const {
        const bool terminal = _State == eSshState::Closed
            || _State == eSshState::Failed;
        return terminal && (!_Route || _Route->CanDestroy());
    }

    void SshSession::RegisterEvent(
        const eSshSessionEvent event,
        const fSshSessionEvent& callback) {
        const std::size_t eventIndex = static_cast<std::size_t>(event);
        if (!callback || eventIndex >= _Events.size()) {
            return;
        }
        _Events[eventIndex].push_back(callback);
    }

    bool SshSession::Send(const void* const data, const std::uint32_t size) {
        if (_State != eSshState::Connected || !data || size == 0) {
            return false;
        }
        const char* const bytes = static_cast<const char*>(data);
        _SendBuffer.insert(_SendBuffer.end(), bytes, bytes + size);
        DriveStateMachine();
        return true;
    }

    bool SshSession::ResizeTerminal(
        const std::uint32_t columns,
        const std::uint32_t rows) {
        if (_State != eSshState::Connected
            || !_Channel
            || columns == 0
            || rows == 0) {
            return false;
        }
        _PendingTerminalColumns = columns;
        _PendingTerminalRows = rows;
        DriveStateMachine();
        return true;
    }

    void SshSession::Close() {
        if (_State == eSshState::Closed || _State == eSshState::Failed) {
            if (_Route) {
                _Route->Close();
            }
            return;
        }

        _State = eSshState::Closing;
        CompleteConnect(
            false,
            eSshConnectError::Cancelled,
            "SSH session was cancelled.");
        CleanupChannel();
        if (_Route) {
            _Route->Close();
            if (_Route->CanDestroy()) {
                _State = eSshState::Closed;
            }
        }
        else {
            _State = eSshState::Closed;
        }
    }

    void SshSession::OnRouteConnected(
        const bool succeed,
        const eSshConnectError error,
        const std::string& message) {
        if (!succeed) {
            if (_State != eSshState::Closing && _State != eSshState::Closed) {
                _State = eSshState::Failed;
            }
            CompleteConnect(false, error, message);
            return;
        }
        if (_State != eSshState::Created || !_Route || !_Route->TargetSession()) {
            Fail(
                eSshConnectError::InternalError,
                "SSH route completed without a target connection.");
            return;
        }
        _State = eSshState::OpeningChannel;
        DriveStateMachine();
    }

    void SshSession::OnRouteDisconnected() {
        const bool wasConnected = _Connected;
        CleanupChannel();
        _Connected = false;
        _State = eSshState::Closed;
        CompleteConnect(
            false,
            eSshConnectError::TcpConnectFailed,
            "SSH route was disconnected.");
        if (wasConnected && !_DisconnectEventCalled) {
            _DisconnectEventCalled = true;
            NotifyEvent(eSshSessionEvent::Disconnected, nullptr, 0);
        }
    }

    void SshSession::OnConnectTimeout() {
        std::string stage = "establishing the SSH route";
        switch (_State) {
        case eSshState::OpeningChannel:
            stage = "opening the shell channel";
            break;
        case eSshState::RequestingPty:
            stage = "requesting the SSH terminal";
            break;
        case eSshState::StartingShell:
            stage = "starting the remote shell";
            break;
        default:
            break;
        }
        Fail(
            eSshConnectError::Timeout,
            "SSH shell connection timed out while " + stage + ".");
    }

    void SshSession::DriveStateMachine() {
        if (_Driving) {
            _DrivePending = true;
            return;
        }
        if (!_Route || !_Route->TargetSession()) {
            return;
        }

        do {
            _DrivePending = false;
            _Driving = true;
            bool continueDriving = true;
            LIBSSH2_SESSION* const session = _Route->TargetSession();

            for (int iteration = 0; iteration < 16 && continueDriving; ++iteration) {
                switch (_State) {
                case eSshState::OpeningChannel:
                    _Channel = libssh2_channel_open_session(session);
                    if (!_Channel) {
                        if (libssh2_session_last_errno(session) == LIBSSH2_ERROR_EAGAIN) {
                            _Route->HandleWouldBlock();
                            continueDriving = false;
                        }
                        else {
                            Fail(
                                eSshConnectError::ChannelFailed,
                                GetSessionError("Failed to open SSH shell channel."));
                            continueDriving = false;
                        }
                    }
                    else {
                        _Route->NotifyProgress();
                        _State = eSshState::RequestingPty;
                    }
                    break;
                case eSshState::RequestingPty: {
                    const int result = libssh2_channel_request_pty(
                        _Channel,
                        "xterm-256color");
                    if (result == LIBSSH2_ERROR_EAGAIN) {
                        _Route->HandleWouldBlock();
                        continueDriving = false;
                    }
                    else if (result != 0) {
                        Fail(
                            eSshConnectError::ChannelFailed,
                            GetSessionError("Failed to request SSH terminal."));
                        continueDriving = false;
                    }
                    else {
                        _Route->NotifyProgress();
                        _State = eSshState::StartingShell;
                    }
                    break;
                }
                case eSshState::StartingShell: {
                    const int result = libssh2_channel_shell(_Channel);
                    if (result == LIBSSH2_ERROR_EAGAIN) {
                        _Route->HandleWouldBlock();
                        continueDriving = false;
                    }
                    else if (result != 0) {
                        Fail(
                            eSshConnectError::ChannelFailed,
                            GetSessionError("Failed to start SSH shell."));
                        continueDriving = false;
                    }
                    else {
                        _Route->NotifyProgress();
                        _State = eSshState::Connected;
                        _Connected = true;
                        _Route->CompleteConnectionSetup();
                        CompleteConnect(
                            true,
                            eSshConnectError::None,
                            std::string());
                        if (_State == eSshState::Connected) {
                            NotifyEvent(eSshSessionEvent::Connected, nullptr, 0);
                        }
                    }
                    break;
                }
                case eSshState::Connected:
                    if (!ApplyTerminalResize()) {
                        continueDriving = false;
                        break;
                    }
                    if (!FlushChannelWrites()) {
                        continueDriving = false;
                        break;
                    }
                    if (_SendOffset < _SendBuffer.size()) {
                        _Route->HandleWouldBlock();
                        continueDriving = false;
                        break;
                    }
                    if (!ReadChannelOutput()) {
                        continueDriving = false;
                    }
                    continueDriving = false;
                    break;
                default:
                    continueDriving = false;
                    break;
                }
            }
            _Driving = false;
        } while (_DrivePending && _Route && _Route->TargetSession());
    }

    bool SshSession::ApplyTerminalResize() {
        if (!_Channel
            || _PendingTerminalColumns == 0
            || _PendingTerminalRows == 0) {
            return true;
        }
        const int result = libssh2_channel_request_pty_size_ex(
            _Channel,
            static_cast<int>(_PendingTerminalColumns),
            static_cast<int>(_PendingTerminalRows),
            0,
            0);
        if (result == LIBSSH2_ERROR_EAGAIN) {
            _Route->HandleWouldBlock();
            return false;
        }
        if (result != 0) {
            Fail(
                eSshConnectError::ChannelFailed,
                GetSessionError("Failed to resize SSH terminal."));
            return false;
        }
        _PendingTerminalColumns = 0;
        _PendingTerminalRows = 0;
        _Route->NotifyProgress();
        return true;
    }

    bool SshSession::FlushChannelWrites() {
        while (_SendOffset < _SendBuffer.size()) {
            const char* const data = _SendBuffer.data() + _SendOffset;
            const std::size_t remaining = _SendBuffer.size() - _SendOffset;
            const ssize_t result = libssh2_channel_write_ex(
                _Channel,
                0,
                data,
                remaining);
            if (result == LIBSSH2_ERROR_EAGAIN) {
                _Route->HandleWouldBlock();
                return true;
            }
            if (result < 0) {
                Fail(
                    eSshConnectError::ChannelFailed,
                    GetSessionError("Failed to write SSH shell channel."));
                return false;
            }
            _SendOffset += static_cast<std::size_t>(result);
            _Route->NotifyProgress();
        }
        if (_SendOffset == _SendBuffer.size()) {
            _SendBuffer.clear();
            _SendOffset = 0;
        }
        return true;
    }

    bool SshSession::ReadChannelOutput() {
        char buffer[16 * 1024];
        for (const int streamId : {0, SSH_EXTENDED_DATA_STDERR}) {
            for (int iteration = 0; iteration < 16; ++iteration) {
                const ssize_t result = libssh2_channel_read_ex(
                    _Channel,
                    streamId,
                    buffer,
                    sizeof(buffer));
                if (result > 0) {
                    _Route->NotifyProgress();
                    NotifyEvent(
                        eSshSessionEvent::Receive,
                        buffer,
                        static_cast<std::uint32_t>(result));
                    if (_State != eSshState::Connected) {
                        return false;
                    }
                    continue;
                }
                if (result == 0 || result == LIBSSH2_ERROR_EAGAIN) {
                    if (result == LIBSSH2_ERROR_EAGAIN) {
                        _Route->HandleWouldBlock();
                    }
                    break;
                }
                Fail(
                    eSshConnectError::ChannelFailed,
                    GetSessionError("Failed to read SSH shell channel."));
                return false;
            }
        }
        if (_Channel && libssh2_channel_eof(_Channel)) {
            Close();
            return false;
        }
        return true;
    }

    void SshSession::NotifyEvent(
        const eSshSessionEvent event,
        const void* const data,
        const std::uint32_t size) {
        const std::size_t eventIndex = static_cast<std::size_t>(event);
        if (eventIndex >= _Events.size()) {
            return;
        }
        const std::vector<fSshSessionEvent> callbacks = _Events[eventIndex];
        for (const fSshSessionEvent& callback : callbacks) {
            try {
                callback(this, data, size);
            }
            catch (...) {
            }
        }
    }

    void SshSession::CompleteConnect(
        const bool success,
        const eSshConnectError error,
        const std::string& message) {
        if (_ConnectCallbackCalled) {
            return;
        }
        _ConnectCallbackCalled = true;
        const ISshApi::fSshConnect callback = std::move(_ConnectCallback);
        _ConnectCallback = nullptr;
        if (!callback) {
            return;
        }
        try {
            callback(success, success ? this : nullptr, error, message);
        }
        catch (...) {
        }
    }

    void SshSession::Fail(
        const eSshConnectError error,
        const std::string& message) {
        if (_State == eSshState::Failed || _State == eSshState::Closed) {
            return;
        }
        _State = eSshState::Failed;
        CleanupChannel();
        CompleteConnect(false, error, message);
        if (_Route) {
            _Route->Close();
        }
    }

    void SshSession::CleanupChannel() {
        LIBSSH2_CHANNEL* const channel = _Channel;
        _Channel = nullptr;
        if (channel) {
            (void)libssh2_channel_free(channel);
        }
    }

    std::string SshSession::GetSessionError(const std::string& fallback) const {
        LIBSSH2_SESSION* const session = _Route ? _Route->TargetSession() : nullptr;
        if (!session) {
            return fallback;
        }
        char* errorMessage = nullptr;
        int errorLength = 0;
        libssh2_session_last_error(
            session,
            &errorMessage,
            &errorLength,
            0);
        if (!errorMessage || errorLength <= 0) {
            return fallback;
        }
        return std::string(errorMessage, static_cast<std::size_t>(errorLength));
    }
}
