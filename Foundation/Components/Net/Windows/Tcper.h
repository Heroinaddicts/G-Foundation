#ifndef __Tcper_h__
#define __Tcper_h__

#include "Header.h"

#include <atomic>

namespace Foundation {
    class Tcper;
    extern FoundationUtils::XPool<Tcper> s_TcperPool;
    class Net;
    class Tcper : public iCompleted, public ITcpPipe {
    public:
        virtual ~Tcper();


        static Tcper* Create(ITcpSession* session, const std::string& ip, const UInt16 port, int sock, bool isActiveConnect, const int size);

        virtual void OnCompleted(OVERLAPPEDEX* ex, const eCompletion type, const int code, const int size);
        virtual void Pause();
        virtual void Resume();
        virtual void Close();
        virtual void Send(const void* data, const int size, bool immediately);


        std::string RemoteIp() const override;
        int RemotePort() const override;
        bool IsActiveConnect() const override;
        void DetachSession(ITcpSession* session) override;

        void CloseSocket();
        void AsyncSend();
    private:
        friend FoundationUtils::XPool<Tcper>;
        Tcper(ITcpSession* session, int socket, bool isActiveConnect, const int size);

    private:
        bool AsyncRecv();
        void TryFinalizeClose();

    private:
        std::atomic_bool _IsSending;
        std::atomic_bool _IsRecving;
        std::atomic_bool _IsClosing;
        std::atomic_bool _IsFinalized;
        bool _IsCached;

        char _RecvTemp[PACK_MAX_SIZE];

        FoundationUtils::XBuffer _SendBuff;
        FoundationUtils::XBuffer _RecvBuff;

        OVERLAPPEDEX _SendEx;
        OVERLAPPEDEX _RecvEx;

        ITcpSession* _Session;
        const bool _IsActiveConnect;
        int _Socket;
        std::string _RemoteIP;
        UInt16 _RemotePort;

        Net* _Net;
    };
}

#endif //__Tcper_h__
