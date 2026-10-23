#include "Tcper.h"
#include "Net.h"

namespace Foundation {

    FoundationUtils::XPool<Tcper> s_TcperPool;

    Tcper::~Tcper() {
    }

    Tcper::Tcper(ITcpSession* session, int socket, bool isActiveConnect, const int size)
        : _Session(session), _Socket(socket),
        _SendEx(eCompletion::Sended, this), _RecvEx(eCompletion::Recved, this),
        _IsRecving(false), _IsSending(false), _IsCached(false), _IsClosing(false),
        _IsFinalized(false), _IsActiveConnect(isActiveConnect), _RemoteIP("unknown"),
        _RemotePort(-1), _SendBuff(size, size), _RecvBuff(size, size) {

        _Net = dynamic_cast<Net*>(Net::GetInstance());

        FoundationUtils::SafeMemory::Memset(_RecvTemp, sizeof(_RecvTemp), 0, sizeof(_RecvTemp));
        FoundationAssert(_IsRecving == false && _IsSending == false && _IsCached == false, "wtf");
        _RecvEx._Wbuff.buf = _RecvTemp;
        _RecvEx._Wbuff.len = sizeof(_RecvTemp);
    }

    Tcper* Tcper::Create(ITcpSession* session, const std::string& ip, const UInt16 port, int sock, bool isActiveConnect, const int size) {
        Tcper* pipe = XPOOL_CREATE(s_TcperPool, session, sock, isActiveConnect, size);

        if (isActiveConnect) {
            static LPFN_CONNECTEX connectex = GetConnectExFun();
            OVERLAPPEDEX* ex = XPOOL_CREATE(g_OverlappedexPool, eCompletion::Connected, pipe);
            FoundationUtils::SafeMemory::Memset(&ex->_Ol, sizeof(ex->_Ol), 0, sizeof(ex->_Ol));
            FoundationUtils::SafeMemory::Memset(&ex->_Remote, sizeof(ex->_Remote), 0, sizeof(ex->_Remote));
            ex->_Remote.sin_addr.s_addr = inet_addr(ip.c_str());
            ex->_Remote.sin_port = htons(port);
            ex->_Remote.sin_family = AF_INET;
            ex->_Socket = sock;
            DWORD bytes = 0;
            int res = connectex(pipe->_Socket, (struct sockaddr*)&ex->_Remote, sizeof(struct sockaddr_in), nullptr, 0, &bytes, (LPOVERLAPPED)ex);
            int err = GetLastError();
            if (SOCKET_ERROR == res && err != WSA_IO_PENDING) {
                XPOOL_RELEASE(g_OverlappedexPool, ex);
                XPOOL_RELEASE(s_TcperPool, pipe);
                return nullptr;
            }
        }
        else {
            GetRemoteIpAndPort(sock, pipe->_RemoteIP, pipe->_RemotePort);
            if (false == pipe->AsyncRecv()) {
                SAFE_CLOSE_SOCKET(pipe->_Socket);
                XPOOL_RELEASE(s_TcperPool, pipe);
                return nullptr;
            }
        }

        return pipe;
    }

    void Tcper::OnCompleted(OVERLAPPEDEX* ex, const eCompletion type, const int code, const int size) {
        FoundationAssert(type == eCompletion::Recved || type == eCompletion::Sended || type == eCompletion::Connected, "Tcper on completer type error");
        switch (type) {
        case eCompletion::Connected: {
            XPOOL_RELEASE(g_OverlappedexPool, ex);
            DWORD ul = 1;
            if (ERROR_SUCCESS == code
                && SOCKET_ERROR != setsockopt(_Socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0)
                && SOCKET_ERROR != setsockopt(_Socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&ul, sizeof(ul))) {
                if (AsyncRecv()) {
                    GetRemoteIpAndPort(_Socket, _RemoteIP, _RemotePort);
                    _Session->SetPipe(this);
                    _Session->OnTcpSessionEvent(eTcpSessionEvent::Connected);
                    return;
                }
            }

            SAFE_CLOSE_SOCKET(_Socket);
            if (_Session) {
                if (_Session->_Pipe == this) {
                    _Session->SetPipe(nullptr);
                }
                _Session->OnTcpSessionEvent(eTcpSessionEvent::Connectfaild);
            }
            XPOOL_RELEASE(s_TcperPool, this);
            break;
        }
        case eCompletion::Recved: {
            _IsRecving = false;
            if (_IsClosing) {
                TryFinalizeClose();
                return;
            }
            if (0 == code && 0 != size) {
                _RecvBuff.Write(_RecvTemp, size, __FILE__, __LINE__);
                if (!AsyncRecv()) {
                    Close();
                    return;
                }

                while (
                    _RecvBuff.Read(
                        [&](const void* const data, const UInt32 size) -> UInt32 {
                            UInt32 ret = _Session->OnReceive(data, size);
                            return ret;
                        }
                    )) {
                }
            }
            else {
                Close();
            }
            break;
        }
        case eCompletion::Sended: {
            _IsSending = false;
            if (_IsClosing) {
                TryFinalizeClose();
                return;
            }
            if (0 == code) {
                _SendBuff.ReadOut(size);
                _SendBuff.Read(
                    [&](const void* const data, const UInt32 len) ->UInt32 {
                        if (len > 0) {
                            this->AsyncSend();
                        }
                        return 0;
                    }
                );
            }
            else {
                Close();
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
        CloseSocket();
    }

    void Tcper::CloseSocket() {
        if (!_IsClosing.exchange(true, std::memory_order_acq_rel)) {
            SAFE_CLOSE_SOCKET(_Socket);
        }
        TryFinalizeClose();
    }

    void Tcper::TryFinalizeClose() {
        if (!_IsClosing.load(std::memory_order_acquire)
            || _IsSending.load(std::memory_order_acquire)
            || _IsRecving.load(std::memory_order_acquire)) {
            return;
        }
        if (_IsFinalized.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        if (_Session) {
            if (_Session->_Pipe == this) {
                _Session->SetPipe(nullptr);
            }
            _Session->OnTcpSessionEvent(eTcpSessionEvent::Disconnected);
        }
        if (_Net) {
            _Net->RetireTcpPipe(this);
        }
        else {
            XPOOL_RELEASE(s_TcperPool, this);
        }
    }

    void Tcper::Send(const void* data, const int size, bool immediately) {
        if (_IsClosing) {
            return;
        }
        _SendBuff.Write(data, size, __FILE__, __LINE__);
        if (immediately) {
            AsyncSend();
        }
    }

    bool Tcper::AsyncRecv() {
        FoundationAssert(false == _IsRecving, "wtf");
        _IsRecving.store(true, std::memory_order_release);
        SetLastError(0);
        DWORD bytes, flag = 0;
        FoundationUtils::SafeMemory::Memset(&_RecvEx._Ol, sizeof(_RecvEx._Ol), 0, sizeof(_RecvEx._Ol));
        int res = WSARecv(_Socket, &_RecvEx._Wbuff, 1, &bytes, &flag, (LPWSAOVERLAPPED)&_RecvEx, nullptr);
        int err = GetLastError();
        if (SOCKET_ERROR == res && ERROR_IO_PENDING != err) {
            _IsRecving.store(false, std::memory_order_release);
            return false;
        }
        return true;
    }

    std::string Tcper::RemoteIp() const {
        return _RemoteIP;
    }

    int Tcper::RemotePort() const {
        return _RemotePort;
    }

    bool Tcper::IsActiveConnect() const {
        return _IsActiveConnect;
    }

    void Tcper::DetachSession(ITcpSession* const session) {
        if (_Session == session) {
            _Session->SetPipe(nullptr);
            _Session = nullptr;
        }
        CloseSocket();
    }

#define MAX_SEND_LEN 16384

    void Tcper::AsyncSend() {
        if (_IsClosing || _IsSending) {
            return;
        }

        bool failed = false;
        _SendBuff.Read(
            [&](const void* const data, const UInt32 len) -> UInt32 {
                if (len > 0) {
                    FoundationUtils::SafeMemory::Memset(&_SendEx._Ol, sizeof(_SendEx._Ol), 0, sizeof(_SendEx._Ol));
                    _SendEx._Wbuff.buf = (char*)data;
                    _SendEx._Wbuff.len = len <= MAX_SEND_LEN ? len : MAX_SEND_LEN;
                    _IsSending.store(true, std::memory_order_release);
                    if (WSASend(_Socket, &_SendEx._Wbuff, 1, nullptr, 0, (LPWSAOVERLAPPED)&_SendEx, nullptr) != 0) {
                        const int err = GetLastError();
                        if (WSA_IO_PENDING != err) {
                            failed = true;
                            _IsSending.store(false, std::memory_order_release);
                        }
                    }
                }
                else {
                    _IsSending.store(false, std::memory_order_release);
                }

                return 0;
            }
        );

        if (failed) {
            CloseSocket();
        }
    }
}
