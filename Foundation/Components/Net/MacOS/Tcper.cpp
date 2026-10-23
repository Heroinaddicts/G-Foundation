
#include "Tcper.h"
#include "Net.h"

#include <new>

namespace Foundation {
    Tcper::Tcper(Net* owner, int socket, const std::string& ip, const UInt16 port, const bool active, const int size)
        : _IP(ip), _Port(port), _IsActiveConnect(active), _Associat(eCompletion::doIO, this), _Sock(socket), _IsClosed(false), _Session(nullptr), _SendBuffer(size, size), _RecvBuffer(size, size), _Owner(owner) {
    }

    Tcper* Tcper::Create(Net* owner, int socket, const std::string& host, const UInt16 port, bool active, ITcpSession* const session, const int size) {
        if (!owner) {
            return nullptr;
        }
        Tcper* tcper = nullptr;
        if (active) {
            const int hostlen = host.size() + 1;
            char* temp = (char*)alloca(hostlen);
            SafeMemory::Memcpy(temp, hostlen, host.c_str(), hostlen);
            std::string ip;
            if (!GetIpByHost(host.c_str(), ip)) {
                session->OnTcpSessionEvent(eTcpSessionEvent::Connectfaild);
                return nullptr;
            }

            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            int sock = INVALID_FD;
            if (INVALID_FD == (sock = ::socket(AF_INET, SOCK_STREAM, 0))
                || ERROR_SOCKET == SetNonBlocking(sock)
                || inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
                CLOSE_SOCKET(sock);
                session->OnTcpSessionEvent(eTcpSessionEvent::Connectfaild);
                return nullptr;
            }

            int ret = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
            if (SUCCESS == ret) {
                tcper = new (std::nothrow) Tcper(owner, sock, host, port, active, size);
                if (!tcper || ERROR_SOCKET == owner->AddKqueueRead(sock, (void*)&tcper->_Associat)) {
                    session->OnTcpSessionEvent(eTcpSessionEvent::Connectfaild);
                    CLOSE_SOCKET(sock);
                    delete tcper;
                    return nullptr;
                }

                owner->RegisterTcper(tcper);
                tcper->CorrelateTcpSession(session);
                session->OnTcpSessionEvent(eTcpSessionEvent::Connected);
            }
            else if (ret < 0 && errno != EINPROGRESS) {
                CLOSE_SOCKET(sock);
                session->OnTcpSessionEvent(eTcpSessionEvent::Connectfaild);
                return nullptr;
            }
            else {
                tcper = new (std::nothrow) Tcper(owner, sock, host, port, active, size);
                Associat* at = tcper ? new (std::nothrow) Associat(eCompletion::doConnect, tcper) : nullptr;
                if (!tcper || !at) {
                    delete at;
                    delete tcper;
                    CLOSE_SOCKET(sock);
                    session->OnTcpSessionEvent(eTcpSessionEvent::Connectfaild);
                    return nullptr;
                }
                // A pending non-blocking connect is already owned by the
                // logical session. Keep the reverse link valid so timeout or
                // destruction can cancel the transport while EINPROGRESS.
                tcper->CorrelateTcpSession(session);
                if (ERROR_SOCKET == owner->AddKqueueWrite(sock, (void*)at)) {
                    tcper->CorrelateTcpSession(nullptr);
                    CLOSE_SOCKET(sock);
                    session->OnTcpSessionEvent(eTcpSessionEvent::Connectfaild);
                    delete at;
                    delete tcper;
                    return nullptr;
                }
                owner->RegisterTcper(tcper);
            }
        }
        else {
            tcper = new (std::nothrow) Tcper(owner, socket, host, port, active, size);
            if (!tcper || ERROR_SOCKET == owner->AddKqueueRead(socket, (void*)&tcper->_Associat)) {
                delete tcper;
                FError("AddKqueueRead(sock) error, %s", strerror(errno));
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
            tcper->_Owner->DeleteKqueueReadWrite(tcper->_Sock);
        }
        CLOSE_SOCKET(tcper->_Sock);
        delete tcper;
    }

    void Tcper::ProcessData(const TcpSessionDispatchHandle& dispatchHandle) {
        if (!_IsClosed.load(std::memory_order_acquire)) {
            if (dispatchHandle) {
                while (
                    !_IsClosed.load(std::memory_order_acquire)
                    && _RecvBuffer.Read([&](const void* const data, const UInt32 size) {
                        int consumed = 0;
                        return ITcpSession::DispatchReceive(
                            dispatchHandle,
                            data,
                            static_cast<int>(size),
                            OUT consumed
                        ) ? consumed : 0;
                    })
                ) {}
            }
        }
    }

    void Tcper::OnCompleter(Associat* at, const eCompletion type, const struct kevent& kev)
    {
        if (_IsClosed.load(std::memory_order_acquire))
            return;

        switch (type) {
        case eCompletion::doConnect: {
            delete at;
            socklen_t len = sizeof(int);
            _Owner->DeleteKqueueEvent(_Sock, EVFILT_WRITE);
            if ((kev.flags & (EV_ERROR | EV_EOF)) || ERROR_SOCKET == SetTcpNoDelay(_Sock)) {
                _IsClosed.store(true, std::memory_order_release);
                NetEvent* ev = txnew NetEvent(eNetEventType::Connect);
                getsockopt(_Sock, SOL_SOCKET, SO_ERROR, &ev->_Code, &len);
                ev->_Code == 0 ? ev->_Code = -1 : ev->_Code = ev->_Code;
                ev->_Tcper = this;
                ev->_SessionDispatch = GetTcpSessionDispatch();
                _Owner->PushEvent(ev);
                CLOSE_SOCKET(_Sock);
                return;
            }

            if (kev.filter == EVFILT_WRITE) {
                NetEvent* ev = txnew NetEvent(eNetEventType::Connect);
                ev->_Tcper = this;
                ev->_SessionDispatch = GetTcpSessionDispatch();

                if (ERROR_SOCKET == _Owner->AddKqueueRead(_Sock, (void*)&_Associat)) {
                    _IsClosed.store(true, std::memory_order_release);
                    getsockopt(_Sock, SOL_SOCKET, SO_ERROR, &ev->_Code, &len);
                    ev->_Code == 0 ? ev->_Code = -1 : ev->_Code = ev->_Code;
                    CLOSE_SOCKET(_Sock);
                }

                _Owner->PushEvent(ev);
            }
            else {
                _IsClosed.store(true, std::memory_order_release);
                NetEvent* ev = txnew NetEvent(eNetEventType::Connect);
                getsockopt(_Sock, SOL_SOCKET, SO_ERROR, &ev->_Code, &len);
                ev->_Code == 0 ? ev->_Code = -1 : ev->_Code = ev->_Code;
                ev->_Tcper = this;
                ev->_SessionDispatch = GetTcpSessionDispatch();
                _Owner->PushEvent(ev);
                CLOSE_SOCKET(_Sock);
            }
            break;
        }
        case eCompletion::doIO: {
            if (kev.flags & (EV_ERROR | EV_EOF)) {
                {
                    AUTO_LOCK(_Lock);
                    if (!_IsClosed.load(std::memory_order_acquire)) {
                        _Owner->DeleteKqueueReadWrite(_Sock);
                        CLOSE_SOCKET(_Sock);
                        _IsClosed.store(true, std::memory_order_release);
                        NetEvent* ev = txnew NetEvent(eNetEventType::Disconnect);
                        ev->_Tcper = this;
                        ev->_SessionDispatch = GetTcpSessionDispatch();
                        _Owner->PushEvent(ev);
                        DISCONNECTED_COUNT;
                    }
                }
            }
            else {
                if (kev.filter == EVFILT_READ) {
                    bool isRecv = false, isDisconnect = false;
                    int len = 0;
                    do {
                        len = recv(_Sock, _RecvTemp, sizeof(_RecvTemp), 0);
                        if (len < 0 && (EAGAIN == errno || EWOULDBLOCK == errno)) {
                            break;
                        }

                        if (len > 0) {
                            _RecvBuffer.Write(_RecvTemp, len, __FILE__, __LINE__);
                            isRecv = true;
                        }

                        if (len == 0) {
                            isDisconnect = true;
                        }

                    } while (len > 0);

                    if (isRecv && !isDisconnect) {
                        AUTO_LOCK(_Lock);
                        NetEvent* ev = txnew NetEvent(eNetEventType::Recv);
                        ev->_Tcper = this;
                        ev->_SessionDispatch = GetTcpSessionDispatch();
                        _Owner->PushEvent(ev);
                    }

                    if (isDisconnect) {
                        AUTO_LOCK(_Lock);
                        if (!_IsClosed.load(std::memory_order_acquire)) {
                            _IsClosed.store(true, std::memory_order_release);
                            _Owner->DeleteKqueueReadWrite(_Sock);
                            NetEvent* ev = txnew NetEvent(eNetEventType::Disconnect);
                            DISCONNECTED_COUNT;
                            ev->_Tcper = this;
                            ev->_SessionDispatch = GetTcpSessionDispatch();
                            _Owner->PushEvent(ev);
                        }
                    }
                }

                if (
                    !_IsClosed.load(std::memory_order_acquire)
                    && kev.filter == EVFILT_WRITE
                ) {
                    if (_SendBuffer.GetDataSize() > 0) {
                        _SendBuffer.Read([&](const void* const data, const UInt32 size) {
                            int len = ::send(_Sock, data, size, 0);
                            if (len < 0 && (EAGAIN == errno || EWOULDBLOCK == errno || EINTR == errno)) {
                                return 0;
                            }

                            if (len < 0) {
                                AUTO_LOCK(_Lock);
                                if (!_IsClosed.exchange(
                                        true,
                                        std::memory_order_acq_rel
                                    )) {
                                    _Owner->DeleteKqueueReadWrite(_Sock);
                                    CLOSE_SOCKET(_Sock);
                                    NetEvent* ev = txnew NetEvent(
                                        eNetEventType::Disconnect
                                    );
                                    DISCONNECTED_COUNT;
                                    ev->_Tcper = this;
                                    ev->_SessionDispatch = GetTcpSessionDispatch();
                                    _Owner->PushEvent(ev);
                                }
                                return 0;
                            }

                            return len;
                            });
                    }
                    if (_SendBuffer.GetDataSize() == 0) {
                        _Owner->DeleteKqueueEvent(_Sock, EVFILT_WRITE);
                    }
                }
            }
            break;
        }
        }
    }

    void Tcper::Pause() {

    }

    void Tcper::Resume() {

    }

    void Tcper::Close() {
        AUTO_LOCK(_Lock);
        if (!_IsClosed.load(std::memory_order_acquire)) {
            const TcpSessionDispatchHandle dispatchHandle = GetTcpSessionDispatch();
            CorrelateTcpSession(nullptr);
            _Owner->DeleteKqueueReadWrite(_Sock);
            CLOSE_SOCKET(_Sock);
            _IsClosed.store(true, std::memory_order_release);
            NetRequest* req = txnew NetRequest(eNetRequestType::CloseTcper);
            req->_Tcper = this;

            DISCONNECTED_COUNT;
            _Owner->PushRequest(req);

            // A disconnect callback is allowed to retire or destroy its
            // logical session.  Detach it from the transport before invoking
            // user code so the remainder of Close() never dereferences a
            // session whose lifetime may have ended in that callback.
            ITcpSession::DispatchTcpSessionEvent(
                dispatchHandle,
                eTcpSessionEvent::Disconnected
            );
        }
    }

    void Tcper::DetachSession(ITcpSession* const session) {
        AUTO_LOCK(_Lock);
        if (GetTcpSession() == session) {
            CorrelateTcpSession(nullptr);
        }
        if (!_IsClosed.load(std::memory_order_acquire)) {
            _Owner->DeleteKqueueReadWrite(_Sock);
            CLOSE_SOCKET(_Sock);
            _IsClosed.store(true, std::memory_order_release);
            NetRequest* req = txnew NetRequest(eNetRequestType::CloseTcper);
            req->_Tcper = this;
            DISCONNECTED_COUNT;
            _Owner->PushRequest(req);
        }
    }

    void Tcper::Send(const void* data, const int size, bool immediately) {
        if (!_IsClosed.load(std::memory_order_acquire)) {
            _SendBuffer.Write(data, size, __FILE__, __LINE__);
            _Owner->AddKqueueWrite(_Sock, (void*)&_Associat);
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
}
