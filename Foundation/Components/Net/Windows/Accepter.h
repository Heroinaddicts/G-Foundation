#ifndef __Accepter_h__
#define __Accepter_h__

#include "Header.h"

namespace Foundation {
#define accept_temp_size 512

    class Accepter : public iCompleted, public IAccepter {
    public:
        virtual ~Accepter() {}

        static Accepter* Create(ITcpServer* server, const std::string& ip, const UInt16 port, const int size);

        virtual void OnCompleted(OVERLAPPEDEX* ex, const eCompletion type, const int code, const int size);
        virtual void Release();
        bool AsyncAccept();

        ITcpServer* const _Server;
        const std::string _ListenIP;
        const UInt16 _ListenPort;

    private:
        friend FoundationUtils::XPool<Accepter>;
        Accepter(ITcpServer* server, const std::string& ip, const UInt16 port, const int size);

    private:
        char _Temp[accept_temp_size];

    private:
        int _Socket;
        sockaddr_in _Addr;
        OVERLAPPEDEX _Ex;

        const int _Size;
    };
}

#endif //__Accepter_h__
