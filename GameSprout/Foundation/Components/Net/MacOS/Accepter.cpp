#include "Accepter.h"
#include "Net.h"
#include "Tcper.h"

#include <new>

namespace Foundation {
    Accepter* Accepter::Create(Net* owner, ITcpServer* server, const std::string& listenIp, const UInt16 listenPort, const int size) {
        if (!owner || !server) {
            return nullptr;
        }
        struct sockaddr_in addr;
        SafeMemory::Memset(&addr, sizeof(addr), 0, sizeof(addr));
        addr.sin_family = AF_INET;
        inet_pton(AF_INET, listenIp.c_str(), &addr.sin_addr);
        addr.sin_port = htons(listenPort);

        int sock = INVALID_FD;
        if (INVALID_FD == (sock = socket(AF_INET, SOCK_STREAM, 0))
            || ERROR_SOCKET == SetReuse(sock)
            || ERROR_SOCKET == SetNonBlocking(sock)
            || ERROR_SOCKET == bind(sock, (sockaddr*)&addr, sizeof(addr))
            || ERROR_SOCKET == listen(sock, 200)) {
            FError("socket error %s", strerror(errno));
            CLOSE_SOCKET(sock);
            return nullptr;
        }

        Accepter* ac = new (std::nothrow) Accepter(owner, sock, server, listenIp, listenPort, size);
        if (!ac || ERROR_SOCKET == owner->AddKqueueRead(sock, (void*)&(ac->_Associat))) {
            FError("AddKqueueRead(sock) error %s", strerror(errno));
            if (ac) {
                ac->Release();
            }
            else {
                CLOSE_SOCKET(sock);
            }
            return nullptr;
        }

        owner->RegisterAccepter(ac);
        IAccepter* temp = dynamic_cast<IAccepter*>(ac);
        server->SetAccepter(temp);

        FError("Accepter %s:%d is Created", listenIp.c_str(), listenPort);
        return ac;
    }

    void Accepter::OnCompleter(Associat* at, const eCompletion type, const struct kevent& ev) {
        FoundationAssert(type == eCompletion::doAccept, "Accepter on completer error");
        if (ev.filter == EVFILT_READ && !(ev.flags & EV_ERROR)) {
            struct sockaddr_in addr;
            socklen_t len = sizeof(addr);
            Int64 sock = INVALID_FD;
            int time = 0;
            while ((sock = accept(_Socket, (sockaddr*)&addr, &len)) >= 0 && time++ <= 256) {
                if (ERROR_SOCKET != SetNonBlocking(sock)
                    && ERROR_SOCKET != SetTcpNoDelay(sock)
                    && ERROR_SOCKET != SetReuse(sock)) {

                    struct sockaddr_in sa;
                    int len = sizeof(sa);
                    getpeername(sock, (struct sockaddr*)&sa, (socklen_t*)&len);
                    const std::string ip = inet_ntoa(sa.sin_addr);
                    const int port = ntohs(sa.sin_port);

                    Tcper* tcper = Tcper::Create(_Owner, sock, ip, port, false, nullptr, _TcpBufferSize);
                    if (nullptr == tcper) {
                        CLOSE_SOCKET(sock);
                        continue;
                    }

                    NetEvent* netev = txnew NetEvent(eNetEventType::Accept);
                    netev->_IP = ip;
                    netev->_Port = port;
                    netev->_Accepter = this;
                    netev->_Tcper = tcper;
                    CONNECTED_COUNT;
                    _Owner->PushEvent(netev);
                    continue;
                }
                else {
                    FError("socket opt error %s", strerror(errno));
                    CLOSE_SOCKET(sock);
                }
            }
        }
        else {
            FError("accept error %s", ev.flags & EV_ERROR ? strerror((int)ev.data) : strerror(errno));
        }
    }

    void Accepter::Release() {
        if (_Owner) {
            _Owner->UnregisterAccepter(this);
        }
        if (INVALID_FD != _Socket) {
            if (_Owner) {
                _Owner->DeleteKqueueEvent(_Socket, EVFILT_READ);
            }
            CLOSE_SOCKET(_Socket);
        }

        if (_Server && _Server->_Accepter == this) {
            _Server->SetAccepter(nullptr);
        }

        delete this;
    }

    Accepter::Accepter(Net* owner, const int sock, ITcpServer* server, const std::string& listenIp, const UInt16 listenPort, const int size)
        : _Server(server), _ListenIP(listenIp), _ListenPort(listenPort), _Associat(eCompletion::doAccept, this), _TcpBufferSize(size), _Socket(sock), _Owner(owner) {

    }
}
