#ifndef __Accepter_h__
#define __Accepter_h__

#include "Header.h"

namespace Foundation {
    class Net;
    class Accepter : public iCompleter, public IAccepter {
    public:
        virtual ~Accepter() {}

        static Accepter* Create(Net* owner, ITcpServer* server, const std::string& listenIp, const UInt16 listenPort, const int size);

        virtual void OnCompleter(Associat* at, const eCompletion type, const struct epoll_event& ev);
        virtual void Release();

    private:
        Accepter(Net* owner, const int sock, ITcpServer* server, const std::string& listenIp, const UInt16 listenPort, const int size);

    public:
        ITcpServer* const _Server;
        const std::string _ListenIP;
        const UInt16 _ListenPort;
        const Associat _Associat;
        const int _TcpBufferSize;

        int _Socket;
        Net* _Owner;
    };
}

#endif //__Accepter_h__
