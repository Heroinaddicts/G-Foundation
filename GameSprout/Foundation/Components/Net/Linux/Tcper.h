#ifndef __Tcper_h__
#define __Tcper_h__

#include "Header.h"
#include <atomic>

namespace Foundation {
    class Net;
    class Tcper : public iCompleter, public ITcpPipe {
    public:
        virtual ~Tcper() {}

        static Tcper* Create(Net* owner, const int socket, const std::string& host, const UInt16 port, const bool active, ITcpSession* const session, const int size);
        static void Release(Tcper* tcper);

        __forceinline void CorrelateTcpSession(ITcpSession* const session) {
            const TcpSessionDispatchHandle nextDispatch = session
                ? session->CaptureDispatchHandle()
                : TcpSessionDispatchHandle{};
            {
                std::lock_guard<std::mutex> guard(_DispatchHandleMutex);
                _SessionDispatch = nextDispatch;
            }
            ITcpSession* const previous = _Session.exchange(
                session,
                std::memory_order_acq_rel
            );
            if (previous && previous != session) {
                previous->SetPipe(nullptr);
            }
            if (session) {
                session->SetPipe(this);
            }
        }

        __forceinline ITcpSession* GetTcpSession() const {
            return _Session.load(std::memory_order_acquire);
        }

        __forceinline TcpSessionDispatchHandle GetTcpSessionDispatch() const {
            std::lock_guard<std::mutex> guard(_DispatchHandleMutex);
            return _SessionDispatch;
        }

        void ProcessData(const TcpSessionDispatchHandle& dispatchHandle);

        const std::string _IP;
        const int _Port;
        const bool _IsActiveConnect;
        const Associat _Associat;


        void OnCompleter(Associat* at, const eCompletion type, const struct epoll_event& ev) override;

        void Pause() override;
        void Resume() override;
        void Close() override;

        void Send(const void* data, const int size, bool immediately) override;
        std::string RemoteIp() const override;
        int RemotePort() const override;
        bool IsActiveConnect() const override;
        void DetachSession(ITcpSession* session) override;
        __forceinline bool IsClosed() const {
            return _IsClosed.load(std::memory_order_acquire);
        }

    private:
        Tcper(Net* owner, const int sock, const std::string& host, const UInt16 port, const bool active, const int size);

    private:
        int _Sock;
        std::atomic<bool> _IsClosed;
        std::atomic<ITcpSession*> _Session;
        mutable std::mutex _DispatchHandleMutex;
        TcpSessionDispatchHandle _SessionDispatch{};

        char _RecvTemp[RECV_TEMP_SIZE];

        XBuffer _SendBuffer;
        XBuffer _RecvBuffer;

        SpinLock _Lock;
        Net* _Owner;
    };
}

#endif //__Tcper_h__
