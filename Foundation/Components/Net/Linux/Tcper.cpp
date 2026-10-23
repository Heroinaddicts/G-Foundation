#include "Tcper.h"
#include "Net.h"

#include <new>

namespace Foundation {
    Tcper::Tcper(
        Net* owner,
        int socket,
        const std::string& ip,
        const UInt16 port,
        const bool active,
        const int size)
        : _IP(ip),
          _Port(port),
          _IsActiveConnect(active),
          _Associat(eCompletion::doIO, this),
          _Sock(socket),
          _IsClosed(false),
          _Session(nullptr),
          _SendBuffer(size, size),
          _RecvBuffer(size, size),
          _Owner(owner) {
    }

    Tcper* Tcper::Create(
        Net* owner,
        int socket,
        const std::string& host,
        const UInt16 port,
        bool active,
        ITcpSession* const session,
        const int size) {
        if (!owner || (active && !session)) {
            return nullptr;
        }

        Tcper* tcper = nullptr;
        if (active) {
            std::string ip;
            if (!GetIpByHost(host.c_str(), ip)) {
                session->OnTcpSessionEvent(eTcpSessionEvent::Connectfaild);
                return nullptr;
            }

            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            int connectedSocket = INVALID_FD;
            if (INVALID_FD == (connectedSocket = ::socket(AF_INET, SOCK_STREAM, 0))
                || ERROR_SOCKET == SetNonBlocking(connectedSocket)
                || inet_pton(AF_INET, ip.c_str(), &address.sin_addr) <= 0) {
                CLOSE_SOCKET(connectedSocket);
                session->OnTcpSessionEvent(eTcpSessionEvent::Connectfaild);
                return nullptr;
            }

            const int connectResult = connect(
                connectedSocket,
                reinterpret_cast<sockaddr*>(&address),
                sizeof(address));
            if (connectResult == SUCCESS) {
                tcper = new (std::nothrow) Tcper(
                    owner,
                    connectedSocket,
                    host,
                    port,
                    active,
                    size);
                if (!tcper
                    || ERROR_SOCKET == owner->AddEpoll(
                        connectedSocket,
                        EPOLLIN,
                        tcper ? const_cast<Associat*>(&tcper->_Associat) : nullptr)) {
                    session->OnTcpSessionEvent(eTcpSessionEvent::Connectfaild);
                    CLOSE_SOCKET(connectedSocket);
                    delete tcper;
                    return nullptr;
                }

                owner->RegisterTcper(tcper);
                tcper->CorrelateTcpSession(session);
                session->OnTcpSessionEvent(eTcpSessionEvent::Connected);
            }
            else if (connectResult < 0 && errno != EINPROGRESS) {
                CLOSE_SOCKET(connectedSocket);
                session->OnTcpSessionEvent(eTcpSessionEvent::Connectfaild);
                return nullptr;
            }
            else {
                tcper = new (std::nothrow) Tcper(
                    owner,
                    connectedSocket,
                    host,
                    port,
                    active,
                    size);
                Associat* association = tcper
                    ? new (std::nothrow) Associat(eCompletion::doConnect, tcper)
                    : nullptr;
                if (!tcper || !association) {
                    delete association;
                    delete tcper;
                    CLOSE_SOCKET(connectedSocket);
                    session->OnTcpSessionEvent(eTcpSessionEvent::Connectfaild);
                    return nullptr;
                }

                tcper->CorrelateTcpSession(session);
                if (ERROR_SOCKET == owner->AddEpoll(
                        connectedSocket,
                        EPOLLOUT,
                        association)) {
                    tcper->CorrelateTcpSession(nullptr);
                    CLOSE_SOCKET(connectedSocket);
                    session->OnTcpSessionEvent(eTcpSessionEvent::Connectfaild);
                    delete association;
                    delete tcper;
                    return nullptr;
                }
                owner->RegisterTcper(tcper);
            }
        }
        else {
            tcper = new (std::nothrow) Tcper(
                owner,
                socket,
                host,
                port,
                active,
                size);
            if (!tcper
                || ERROR_SOCKET == owner->AddEpoll(
                    socket,
                    EPOLLIN | EPOLLOUT,
                    tcper ? const_cast<Associat*>(&tcper->_Associat) : nullptr)) {
                delete tcper;
                FError("AddEpoll(socket) error, %s", strerror(errno));
                return nullptr;
            }
            owner->RegisterTcper(tcper);
        }

        return tcper;
    }

    void Tcper::Release(Tcper* tcper) {
        if (!tcper) {
            return;
        }
        tcper->_IsClosed.store(true, std::memory_order_release);
        tcper->CorrelateTcpSession(nullptr);
        if (tcper->_Owner) {
            tcper->_Owner->UnregisterTcper(tcper);
            tcper->_Owner->DeleteEpoll(tcper->_Sock);
        }
        if (tcper->_Sock != INVALID_FD) {
            CLOSE_SOCKET(tcper->_Sock);
        }
        delete tcper;
    }

    void Tcper::ProcessData(const TcpSessionDispatchHandle& dispatchHandle) {
        if (_IsClosed.load(std::memory_order_acquire) || !dispatchHandle) {
            return;
        }

        const Int64 tick = SafeSystem::Time::GetMicroSecond();
        while (!_IsClosed.load(std::memory_order_acquire)
            && _RecvBuffer.Read([&](const void* const data, const UInt32 size) {
                int consumed = 0;
                return ITcpSession::DispatchReceive(
                    dispatchHandle,
                    data,
                    static_cast<int>(size),
                    OUT consumed)
                    ? consumed
                    : 0;
            })) {
            if (SafeSystem::Time::GetMicroSecond() - tick > 1000) {
                if (_RecvBuffer.GetDataSize() > 0) {
                    AUTO_LOCK(_Lock);
                    NetEvent* event = txnew NetEvent(eNetEventType::Recv);
                    event->_Tcper = this;
                    event->_SessionDispatch = GetTcpSessionDispatch();
                    _Owner->PushEvent(event);
                }
                break;
            }
        }
    }

    void Tcper::OnCompleter(
        Associat* association,
        const eCompletion type,
        const epoll_event& epollEvent) {
        if (_IsClosed.load(std::memory_order_acquire)) {
            return;
        }

        switch (type) {
        case eCompletion::doConnect: {
            delete association;
            socklen_t errorLength = sizeof(int);
            _Owner->DeleteEpoll(_Sock);
            if ((epollEvent.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
                || ERROR_SOCKET == SetTcpNoDelay(_Sock)) {
                _IsClosed.store(true, std::memory_order_release);
                NetEvent* event = txnew NetEvent(eNetEventType::Connect);
                getsockopt(_Sock, SOL_SOCKET, SO_ERROR, &event->_Code, &errorLength);
                if (event->_Code == 0) {
                    event->_Code = -1;
                }
                event->_Tcper = this;
                event->_SessionDispatch = GetTcpSessionDispatch();
                _Owner->PushEvent(event);
                CLOSE_SOCKET(_Sock);
                return;
            }

            if (epollEvent.events & EPOLLOUT) {
                NetEvent* event = txnew NetEvent(eNetEventType::Connect);
                event->_Tcper = this;
                event->_SessionDispatch = GetTcpSessionDispatch();
                if (ERROR_SOCKET == _Owner->AddEpoll(
                        _Sock,
                        EPOLLIN | EPOLLOUT,
                        const_cast<Associat*>(&_Associat))) {
                    _IsClosed.store(true, std::memory_order_release);
                    getsockopt(_Sock, SOL_SOCKET, SO_ERROR, &event->_Code, &errorLength);
                    if (event->_Code == 0) {
                        event->_Code = -1;
                    }
                    CLOSE_SOCKET(_Sock);
                }
                _Owner->PushEvent(event);
            }
            else {
                _IsClosed.store(true, std::memory_order_release);
                NetEvent* event = txnew NetEvent(eNetEventType::Connect);
                getsockopt(_Sock, SOL_SOCKET, SO_ERROR, &event->_Code, &errorLength);
                if (event->_Code == 0) {
                    event->_Code = -1;
                }
                event->_Tcper = this;
                event->_SessionDispatch = GetTcpSessionDispatch();
                _Owner->PushEvent(event);
                CLOSE_SOCKET(_Sock);
            }
            break;
        }
        case eCompletion::doIO: {
            if (epollEvent.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                AUTO_LOCK(_Lock);
                if (!_IsClosed.exchange(true, std::memory_order_acq_rel)) {
                    _Owner->DeleteEpoll(_Sock);
                    CLOSE_SOCKET(_Sock);
                    NetEvent* event = txnew NetEvent(eNetEventType::Disconnect);
                    event->_Tcper = this;
                    event->_SessionDispatch = GetTcpSessionDispatch();
                    _Owner->PushEvent(event);
                    DISCONNECTED_COUNT;
                }
                break;
            }

            if (epollEvent.events & EPOLLIN) {
                bool received = false;
                bool disconnected = false;
                int length = 0;
                do {
                    length = recv(_Sock, _RecvTemp, sizeof(_RecvTemp), 0);
                    if (length < 0
                        && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        break;
                    }
                    if (length > 0) {
                        _RecvBuffer.Write(_RecvTemp, length, __FILE__, __LINE__);
                        received = true;
                    }
                    if (length == 0) {
                        disconnected = true;
                    }
                } while (length > 0);

                if (received && !disconnected) {
                    AUTO_LOCK(_Lock);
                    NetEvent* event = txnew NetEvent(eNetEventType::Recv);
                    event->_Tcper = this;
                    event->_SessionDispatch = GetTcpSessionDispatch();
                    _Owner->PushEvent(event);
                }

                if (disconnected) {
                    AUTO_LOCK(_Lock);
                    if (!_IsClosed.exchange(true, std::memory_order_acq_rel)) {
                        _Owner->DeleteEpoll(_Sock);
                        NetEvent* event = txnew NetEvent(eNetEventType::Disconnect);
                        event->_Tcper = this;
                        event->_SessionDispatch = GetTcpSessionDispatch();
                        _Owner->PushEvent(event);
                        DISCONNECTED_COUNT;
                    }
                }
            }

            if (!_IsClosed.load(std::memory_order_acquire)
                && (epollEvent.events & EPOLLOUT)
                && _SendBuffer.GetDataSize() > 0) {
                _SendBuffer.Read([&](const void* const data, const UInt32 size) {
                    const int sent = ::send(_Sock, data, size, 0);
                    return sent > 0 ? sent : 0;
                });
            }
            break;
        }
        default:
            break;
        }
    }

    void Tcper::Pause() {
    }

    void Tcper::Resume() {
    }

    void Tcper::Close() {
        AUTO_LOCK(_Lock);
        if (!_IsClosed.exchange(true, std::memory_order_acq_rel)) {
            const TcpSessionDispatchHandle dispatchHandle = GetTcpSessionDispatch();
            CorrelateTcpSession(nullptr);
            _Owner->DeleteEpoll(_Sock);
            CLOSE_SOCKET(_Sock);
            NetRequest* request = txnew NetRequest(eNetRequestType::CloseTcper);
            request->_Tcper = this;
            DISCONNECTED_COUNT;
            _Owner->PushRequest(request);
            ITcpSession::DispatchTcpSessionEvent(
                dispatchHandle,
                eTcpSessionEvent::Disconnected);
        }
    }

    void Tcper::Send(const void* data, const int size, bool immediately) {
        if (!_IsClosed.load(std::memory_order_acquire)) {
            _SendBuffer.Write(data, size, __FILE__, __LINE__);
        }
    }

    std::string Tcper::RemoteIp() const {
        return _IP;
    }

    int Tcper::RemotePort() const {
        return _Port;
    }

    bool Tcper::IsActiveConnect() const {
        return _IsActiveConnect;
    }

    void Tcper::DetachSession(ITcpSession* const session) {
        AUTO_LOCK(_Lock);
        if (GetTcpSession() == session) {
            CorrelateTcpSession(nullptr);
        }
        if (!_IsClosed.exchange(true, std::memory_order_acq_rel)) {
            _Owner->DeleteEpoll(_Sock);
            CLOSE_SOCKET(_Sock);
            NetRequest* request = txnew NetRequest(eNetRequestType::CloseTcper);
            request->_Tcper = this;
            DISCONNECTED_COUNT;
            _Owner->PushRequest(request);
        }
    }
}
