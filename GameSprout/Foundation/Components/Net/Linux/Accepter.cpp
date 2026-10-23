#include "Accepter.h"
#include "Net.h"
#include "Tcper.h"

#include <new>

namespace Foundation {
    Accepter* Accepter::Create(
        Net* owner,
        ITcpServer* server,
        const std::string& listenIp,
        const UInt16 listenPort,
        const int size) {
        if (!owner || !server) {
            return nullptr;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        inet_pton(AF_INET, listenIp.c_str(), &address.sin_addr);
        address.sin_port = htons(listenPort);

        int socket = INVALID_FD;
        if (INVALID_FD == (socket = ::socket(AF_INET, SOCK_STREAM, 0))
            || ERROR_SOCKET == SetReuse(socket)
            || ERROR_SOCKET == SetNonBlocking(socket)
            || ERROR_SOCKET == bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address))
            || ERROR_SOCKET == listen(socket, 200)) {
            FError("socket error %s", strerror(errno));
            CLOSE_SOCKET(socket);
            return nullptr;
        }

        Accepter* accepter = new (std::nothrow) Accepter(
            owner,
            socket,
            server,
            listenIp,
            listenPort,
            size);
        if (!accepter
            || ERROR_SOCKET == owner->AddEpoll(
                socket,
                EPOLLIN,
                const_cast<Associat*>(&accepter->_Associat))) {
            FError("AddEpoll(socket) error %s", strerror(errno));
            if (accepter) {
                accepter->Release();
            }
            else {
                CLOSE_SOCKET(socket);
            }
            return nullptr;
        }

        owner->RegisterAccepter(accepter);
        server->SetAccepter(accepter);
        FError("Accepter %s:%d is Created", listenIp.c_str(), listenPort);
        return accepter;
    }

    void Accepter::OnCompleter(
        Associat* at,
        const eCompletion type,
        const epoll_event& event) {
        FoundationAssert(type == eCompletion::doAccept, "Accepter on completer error");
        if (!(event.events & EPOLLIN)) {
            FError("accept error %s", strerror(errno));
            return;
        }

        sockaddr_in address{};
        socklen_t addressLength = sizeof(address);
        Int64 socket = INVALID_FD;
        int acceptedCount = 0;
        while ((socket = accept(
                    _Socket,
                    reinterpret_cast<sockaddr*>(&address),
                    &addressLength)) >= 0
            && acceptedCount++ <= 256) {
            if (ERROR_SOCKET != SetNonBlocking(socket)
                && ERROR_SOCKET != SetTcpNoDelay(socket)
                && ERROR_SOCKET != SetReuse(socket)
                && ERROR_SOCKET != SetSockSendBuff(socket, 0)
                && ERROR_SOCKET != SetSockRecvBuff(socket, 0)) {
                sockaddr_in peerAddress{};
                socklen_t peerAddressLength = sizeof(peerAddress);
                getpeername(
                    socket,
                    reinterpret_cast<sockaddr*>(&peerAddress),
                    &peerAddressLength);
                const std::string ip = inet_ntoa(peerAddress.sin_addr);
                const int port = ntohs(peerAddress.sin_port);

                Tcper* tcper = Tcper::Create(
                    _Owner,
                    socket,
                    ip,
                    port,
                    false,
                    nullptr,
                    _TcpBufferSize);
                if (!tcper) {
                    CLOSE_SOCKET(socket);
                    continue;
                }

                NetEvent* netEvent = txnew NetEvent(eNetEventType::Accept);
                netEvent->_IP = ip;
                netEvent->_Port = port;
                netEvent->_Accepter = this;
                netEvent->_Tcper = tcper;
                CONNECTED_COUNT;
                _Owner->PushEvent(netEvent);
                continue;
            }

            FError("socket opt error %s", strerror(errno));
            CLOSE_SOCKET(socket);
        }
    }

    void Accepter::Release() {
        if (_Owner) {
            _Owner->UnregisterAccepter(this);
            _Owner->DeleteEpoll(_Socket);
        }
        if (_Socket != INVALID_FD) {
            CLOSE_SOCKET(_Socket);
        }
        if (_Server && _Server->_Accepter == this) {
            _Server->SetAccepter(nullptr);
        }
        delete this;
    }

    Accepter::Accepter(
        Net* owner,
        const int socket,
        ITcpServer* server,
        const std::string& listenIp,
        const UInt16 listenPort,
        const int size)
        : _Server(server),
          _ListenIP(listenIp),
          _ListenPort(listenPort),
          _Associat(eCompletion::doAccept, this),
          _TcpBufferSize(size),
          _Socket(socket),
          _Owner(owner) {
    }
}
