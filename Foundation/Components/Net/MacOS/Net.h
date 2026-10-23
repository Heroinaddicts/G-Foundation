#ifndef __Net_h__
#define __Net_h__

#include "Header.h"

#include <unordered_set>

namespace Foundation {
    class Net : public INet, public SafeThread {
    public:
        virtual ~Net() {}

        bool Initialize(FoundationApi* const foundation) override;
        bool Launch(FoundationApi* const foundation) override;
        void Release(FoundationApi* const foundation) override;
        void EarlyUpdate(FoundationApi* const foundation) override;
        void Update(FoundationApi* const foundation) override;
        void LaterUpdate(FoundationApi* const foundation) override;

        bool LaunchTcpSession(ITcpSession* session, const std::string& host, const UInt16 port, const int size) override;
        bool LaunchTcpServer(ITcpServer* server, const std::string& listenIp, const UInt16 listenPort, const int size) override;

        int AddKqueueEvent(int fd, int16_t filter, void* userData);
        int DeleteKqueueEvent(int fd, int16_t filter);
        int AddKqueueRead(int fd, void* userData);
        int AddKqueueWrite(int fd, void* userData);
        void DeleteKqueueReadWrite(int fd);
        void PushEvent(NetEvent* event);
        void PushRequest(NetRequest* request);
        void RegisterTcper(Tcper* tcper);
        void UnregisterTcper(Tcper* tcper);
        void RegisterAccepter(Accepter* accepter);
        void UnregisterAccepter(Accepter* accepter);

    protected:
        void Run(const int threadIndex, void* const context) override;

    private:
        FoundationApi* _Foundation = nullptr;
        int _KqueueFd = INVALID_FD;
        SafeQueue::SpscQueue<NetEvent*> _EventQueue;
        SafeQueue::SpscQueue<NetRequest*> _RequestQueue;
        SpinLock _ObjectsLock;
        std::unordered_set<Tcper*> _Tcpers;
        std::unordered_set<Accepter*> _Acceptors;
    };
}

#endif //__Net_h__
