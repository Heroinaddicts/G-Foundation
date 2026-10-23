#include "Accepter.h"
#include "Tcper.h"
namespace Foundation {
    static FoundationUtils::XPool<Accepter> s_AccepterPool;

    Accepter* Accepter::Create(ITcpServer* server, const std::string& ip, const UInt16 port, const int size) {
        Accepter* ac = XPOOL_CREATE(s_AccepterPool, server, ip, port, size);
        const auto failCreate = [&](const char* stage, const int errorCode) -> Accepter* {
            if (ac->_Socket != INVALID_SOCKET) {
                SAFE_CLOSE_SOCKET(ac->_Socket);
            }

            XPOOL_RELEASE(s_AccepterPool, ac);
            server->OnError(nullptr);
            return nullptr;
            };

        FoundationUtils::SafeMemory::Memset(&ac->_Addr, sizeof(ac->_Addr), 0, sizeof(ac->_Addr));
        inet_pton(AF_INET, ip.c_str(), (void*)&ac->_Addr.sin_addr.s_addr);
        ac->_Addr.sin_family = AF_INET;
        ac->_Addr.sin_port = htons(port);

        SetLastError(0);
        int len = 0;
        ac->_Socket = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
        if (INVALID_SOCKET == ac->_Socket) {
            return failCreate("socket", WSAGetLastError());
        }

        if (SOCKET_ERROR == setsockopt(ac->_Socket, SOL_SOCKET, SO_SNDBUF, (char*)&len, sizeof(int))) {
            return failCreate("setsockopt-send-buffer", WSAGetLastError());
        }

        if (SOCKET_ERROR == setsockopt(ac->_Socket, SOL_SOCKET, SO_RCVBUF, (char*)&len, sizeof(int))) {
            return failCreate("setsockopt-recv-buffer", WSAGetLastError());
        }

        if (SOCKET_ERROR == ::bind(ac->_Socket, (struct sockaddr*)&(ac->_Addr), sizeof(sockaddr_in))) {
            return failCreate("bind", WSAGetLastError());
        }

        if (listen(ac->_Socket, 2048) == SOCKET_ERROR) {
            return failCreate("listen", WSAGetLastError());
        }

        FoundationAssert(ac, "wtf");
        if (g_CompletePort != CreateIoCompletionPort((HANDLE)ac->_Socket, g_CompletePort, (u_long)ac->_Socket, 0)) {
            return failCreate("completion-port", static_cast<int>(GetLastError()));
        }

        if (false == ac->AsyncAccept()) {
            server->OnRelease();
            return failCreate("accept", WSAGetLastError());
        }


        IAccepter* iac = ac;
        server->SetAccepter(iac);
        return ac;
    }

    void Accepter::OnCompleted(OVERLAPPEDEX* ex, const eCompletion type, const int code, const int size) {
        FoundationAssert(type == eCompletion::Accepted, "accepter error on completer type");
        if (0 == code) {
            DWORD ul = 1;
            if (SOCKET_ERROR == setsockopt(_Ex._Socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&_Socket, sizeof(SOCKET))
                || CreateIoCompletionPort((HANDLE)_Ex._Socket, (HANDLE)g_CompletePort, (u_long)_Ex._Socket, 0) != g_CompletePort
                || SOCKET_ERROR == ioctlsocket(_Ex._Socket, FIONBIO, &ul)
                || SOCKET_ERROR == setsockopt(_Ex._Socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&ul, sizeof(ul))) {
                SAFE_CLOSE_SOCKET(_Ex._Socket);
            }
            else {
                struct sockaddr_in sa;
                int len = sizeof(sa);
                getpeername(_Ex._Socket, (struct sockaddr*)&sa, &len);
                const std::string ip = inet_ntoa(sa.sin_addr);
                const int port = ntohs(sa.sin_port);

                ITcpSession* session = _Server->OnMallocTcpSession(ip.c_str(), port);
                if (session) {
                    Tcper* pipe = Tcper::Create(session, ip, port, _Ex._Socket, false, _Size);
                    if (pipe) {
                        session->SetPipe(pipe);
                        session->OnTcpSessionEvent(eTcpSessionEvent::Connected);
                    }
                    else {
                        SAFE_CLOSE_SOCKET(_Ex._Socket);
                        _Server->OnError(session);
                    }
                }
                else {
                    SAFE_CLOSE_SOCKET(_Ex._Socket);
                }
            }
        }
        else {
            SAFE_CLOSE_SOCKET(_Ex._Socket);
        }

        if (false == AsyncAccept()) {
            _Server->OnRelease();
            if (_Server->_Accepter == this) {
                _Server->SetAccepter(nullptr);
            }
            XPOOL_RELEASE(s_AccepterPool, this);
        }
    }

    void Accepter::Release() {
        SAFE_CLOSE_SOCKET(_Socket);
        if (_Server && _Server->_Accepter == this) {
            _Server->SetAccepter(nullptr);
        }
        XPOOL_RELEASE(s_AccepterPool, this);
    }

    bool Accepter::AsyncAccept() {
        static LPFN_ACCEPTEX acceptex = GetAcceptExFun();
        DWORD ul = 1;
        int len = 0;
        if (INVALID_SOCKET == (_Ex._Socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED))
            || SOCKET_ERROR == setsockopt(_Ex._Socket, SOL_SOCKET, SO_SNDBUF, (char*)&len, sizeof(int))
            || SOCKET_ERROR == setsockopt(_Ex._Socket, SOL_SOCKET, SO_RCVBUF, (char*)&len, sizeof(int))
            || SOCKET_ERROR == ioctlsocket(_Ex._Socket, FIONBIO, &ul)) {
            SAFE_CLOSE_SOCKET(_Ex._Socket);
            return false;
        }

        DWORD bytes;
        int res = acceptex(
            _Socket,
            _Ex._Socket,
            _Temp,
            0,
            sizeof(struct sockaddr_in) + 16,
            sizeof(struct sockaddr_in) + 16,
            &bytes,
            (LPOVERLAPPED)&_Ex
        );

        if (res == FALSE && GetLastError() != WSA_IO_PENDING) {
            SAFE_CLOSE_SOCKET(_Ex._Socket);
            return false;
        }

        return true;
    }

    Accepter::Accepter(ITcpServer* server, const std::string& ip, const UInt16 port, const int size)
        : _Server(server), _ListenIP(ip), _ListenPort(port), _Socket(INVALID_SOCKET), _Ex(eCompletion::Accepted, this), _Size(size) {
    }
}
