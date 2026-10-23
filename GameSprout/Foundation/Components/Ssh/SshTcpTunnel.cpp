#include "SshTcpTunnel.h"

#include "SshRoute.h"

#include <algorithm>
#include <utility>

namespace Foundation {
    SshTcpTunnel::SshTcpTunnel(
        SshTcpTunnelOptions options,
        ITcpSession* const session)
        : _Options(std::move(options)),
          _Session(session) {
    }

    SshTcpTunnel::~SshTcpTunnel() {
        ITcpSession* const session = DetachBoundSession();
        (void)session;
        CleanupChannel();
        if (_Route) {
            _Route->Close();
            delete _Route;
            _Route = nullptr;
        }
    }

    bool SshTcpTunnel::Start(
        INetApi* const netApi,
        ITimerApi* const timerApi) {
        if (_State != eTunnelState::Created
            || !_Session
            || _Session->_Pipe
            || !netApi
            || !timerApi) {
            return false;
        }

        _Session->SetPipe(this);
        _State = eTunnelState::ConnectingRoute;
        _Route = new SshRoute(
            std::move(_Options._Connection),
            [this](
                const bool succeed,
                const eSshConnectError error,
                const std::string& message) {
                OnRouteConnected(succeed, error, message);
            },
            [this]() {
                Drive();
            },
            [this]() {
                FailConnect();
            },
            [this]() {
                OnRouteDisconnected();
            });
        if (!_Route || !_Route->Start(netApi, timerApi)) {
            if (_State != eTunnelState::Failed
                && _State != eTunnelState::Closed) {
                FailConnect();
            }
            return false;
        }
        return true;
    }

    bool SshTcpTunnel::CanDestroy() const {
        const bool terminal = _State == eTunnelState::Closed
            || _State == eTunnelState::Failed;
        return terminal && (!_Route || _Route->CanDestroy());
    }

    void SshTcpTunnel::Pause() {
        _Paused = true;
    }

    void SshTcpTunnel::Resume() {
        if (!_Paused) {
            return;
        }
        _Paused = false;
        Drive();
    }

    void SshTcpTunnel::Close() {
        if (_State == eTunnelState::Closed
            || _State == eTunnelState::Failed) {
            return;
        }
        const bool wasConnected = _State == eTunnelState::Connected;
        _State = eTunnelState::Closing;
        CleanupChannel();
        ITcpSession* const session = DetachBoundSession();
        if (_Route) {
            _Route->Close();
        }
        _State = eTunnelState::Closed;
        if (session) {
            session->OnTcpSessionEvent(
                wasConnected
                    ? eTcpSessionEvent::Disconnected
                    : eTcpSessionEvent::Connectfaild);
        }
    }

    void SshTcpTunnel::Send(
        const void* const data,
        const int size,
        const bool immediately) {
        (void)immediately;
        if (_State != eTunnelState::Connected || !data || size <= 0) {
            return;
        }
        const char* const bytes = static_cast<const char*>(data);
        _SendBuffer.insert(_SendBuffer.end(), bytes, bytes + size);
        Drive();
    }

    std::string SshTcpTunnel::RemoteIp() const {
        return _Options._DestinationHost;
    }

    int SshTcpTunnel::RemotePort() const {
        return static_cast<int>(_Options._DestinationPort);
    }

    bool SshTcpTunnel::IsActiveConnect() const {
        return true;
    }

    void SshTcpTunnel::DetachSession(ITcpSession* const session) {
        if (!session || session != _Session) {
            return;
        }
        (void)DetachBoundSession();
        CleanupChannel();
        if (_Route) {
            _Route->Close();
        }
        _State = eTunnelState::Closed;
    }

    void SshTcpTunnel::OnRouteConnected(
        const bool succeed,
        const eSshConnectError error,
        const std::string& message) {
        (void)error;
        (void)message;
        if (!succeed) {
            FailConnect();
            return;
        }
        if (_State != eTunnelState::ConnectingRoute
            || !_Route
            || !_Route->TargetSession()) {
            FailConnect();
            return;
        }
        _State = eTunnelState::OpeningChannel;
        Drive();
    }

    void SshTcpTunnel::OnRouteDisconnected() {
        if (_State == eTunnelState::Closed
            || _State == eTunnelState::Failed) {
            return;
        }
        if (_State == eTunnelState::Closing) {
            _State = eTunnelState::Closed;
            return;
        }
        if (_State == eTunnelState::Connected) {
            Disconnect(false, true);
            return;
        }
        FailConnect();
    }

    void SshTcpTunnel::Drive() {
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
            if (_State == eTunnelState::OpeningChannel) {
                LIBSSH2_SESSION* const targetSession =
                    _Route->TargetSession();
                _Channel = libssh2_channel_direct_tcpip_ex(
                    targetSession,
                    _Options._DestinationHost.c_str(),
                    static_cast<int>(_Options._DestinationPort),
                    "127.0.0.1",
                    0);
                if (!_Channel) {
                    if (libssh2_session_last_errno(targetSession)
                        == LIBSSH2_ERROR_EAGAIN) {
                        _Route->HandleWouldBlock();
                    }
                    else {
                        FailConnect();
                    }
                }
                else {
                    _Route->NotifyProgress();
                    _State = eTunnelState::Connected;
                    _Route->CompleteConnectionSetup();
                    ITcpSession* const session = _Session;
                    if (session) {
                        session->OnTcpSessionEvent(eTcpSessionEvent::Connected);
                    }
                }
            }

            if (_State == eTunnelState::Connected
                && (!FlushWrites()
                    || !DeliverReceivedData()
                    || !ReadChannel())) {
                _Driving = false;
                continue;
            }
            _Driving = false;
        } while (_DrivePending
            && _Route
            && _Route->TargetSession()
            && _State != eTunnelState::Closed
            && _State != eTunnelState::Failed);
    }

    bool SshTcpTunnel::FlushWrites() {
        while (_State == eTunnelState::Connected
            && _Channel
            && _SendOffset < _SendBuffer.size()) {
            const ssize_t result = libssh2_channel_write_ex(
                _Channel,
                0,
                _SendBuffer.data() + _SendOffset,
                _SendBuffer.size() - _SendOffset);
            if (result == LIBSSH2_ERROR_EAGAIN || result == 0) {
                _Route->HandleWouldBlock();
                return false;
            }
            if (result < 0) {
                Disconnect(true, true);
                return false;
            }
            _SendOffset += static_cast<std::size_t>(result);
            _Route->NotifyProgress();
        }
        if (_SendOffset == _SendBuffer.size()) {
            _SendBuffer.clear();
            _SendOffset = 0;
        }
        return _State == eTunnelState::Connected;
    }

    bool SshTcpTunnel::DeliverReceivedData() {
        while (!_Paused
            && _State == eTunnelState::Connected
            && _Session
            && _ReceiveOffset < _ReceiveBuffer.size()) {
            const std::size_t available =
                _ReceiveBuffer.size() - _ReceiveOffset;
            const int consumed = _Session->OnReceive(
                _ReceiveBuffer.data() + _ReceiveOffset,
                static_cast<int>(available));
            if (consumed < 0
                || static_cast<std::size_t>(consumed) > available) {
                Disconnect(true, true);
                return false;
            }
            if (consumed == 0) {
                break;
            }
            _ReceiveOffset += static_cast<std::size_t>(consumed);
        }
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
        return !_Paused && _State == eTunnelState::Connected;
    }

    bool SshTcpTunnel::ReadChannel() {
        if (_Paused || !_Channel || !_Session) {
            return false;
        }
        const std::size_t pending =
            _ReceiveBuffer.size() - _ReceiveOffset;
        const std::size_t capacity = static_cast<std::size_t>(
            _Options._ReceiveBufferSize);
        if (pending >= capacity) {
            Disconnect(true, true);
            return false;
        }

        char buffer[16 * 1024];
        for (int iteration = 0; iteration < 16; ++iteration) {
            const std::size_t currentPending =
                _ReceiveBuffer.size() - _ReceiveOffset;
            if (currentPending >= capacity) {
                Disconnect(true, true);
                return false;
            }
            const std::size_t readSize = (std::min)(
                sizeof(buffer),
                capacity - currentPending);
            const ssize_t result = libssh2_channel_read_ex(
                _Channel,
                0,
                buffer,
                readSize);
            if (result > 0) {
                _ReceiveBuffer.insert(
                    _ReceiveBuffer.end(),
                    buffer,
                    buffer + result);
                _Route->NotifyProgress();
                if (!DeliverReceivedData()) {
                    return false;
                }
                if (_Paused) {
                    return false;
                }
                continue;
            }
            if (result == LIBSSH2_ERROR_EAGAIN) {
                _Route->HandleWouldBlock();
                return false;
            }
            if (result == 0 && !libssh2_channel_eof(_Channel)) {
                _Route->HandleWouldBlock();
                return false;
            }
            Disconnect(true, true);
            return false;
        }
        return true;
    }

    void SshTcpTunnel::FailConnect() {
        if (_State == eTunnelState::Failed
            || _State == eTunnelState::Closed) {
            return;
        }
        _State = eTunnelState::Failed;
        CleanupChannel();
        ITcpSession* const session = DetachBoundSession();
        if (_Route) {
            _Route->Close();
        }
        if (session) {
            session->OnTcpSessionEvent(eTcpSessionEvent::Connectfaild);
        }
    }

    void SshTcpTunnel::Disconnect(
        const bool closeRoute,
        const bool notifySession) {
        if (_State == eTunnelState::Closed
            || _State == eTunnelState::Failed) {
            return;
        }
        _State = eTunnelState::Closing;
        CleanupChannel();
        ITcpSession* const session = DetachBoundSession();
        if (closeRoute && _Route) {
            _Route->Close();
        }
        _State = eTunnelState::Closed;
        if (notifySession && session) {
            session->OnTcpSessionEvent(eTcpSessionEvent::Disconnected);
        }
    }

    void SshTcpTunnel::CleanupChannel() {
        if (!_Channel) {
            return;
        }
        LIBSSH2_CHANNEL* const channel = _Channel;
        _Channel = nullptr;
        (void)libssh2_channel_free(channel);
    }

    ITcpSession* SshTcpTunnel::DetachBoundSession() {
        ITcpSession* const session = _Session;
        _Session = nullptr;
        if (session && session->_Pipe == this) {
            session->SetPipe(nullptr);
        }
        return session;
    }

}
