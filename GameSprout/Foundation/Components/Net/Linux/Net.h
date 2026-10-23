#ifndef __Net_h__
#define __Net_h__

#include "Header.h"

#include <unordered_set>

namespace Foundation {
    class Net : public INet, public SafeThread {
    public:
        virtual ~Net() {}

        // 通过 iEngineComponent 继承
        bool Initialize(FoundationApi* const engine) override;
        bool Launch(FoundationApi* const engine) override;
        void Release(FoundationApi* const engine) override;
        void EarlyUpdate(FoundationApi* const engine) override;
        void Update(FoundationApi* const engine) override;
        void LaterUpdate(FoundationApi* const engine) override;

        bool LaunchTcpSession(ITcpSession* session, const std::string& host, const UInt16 port, const int size) override;
        bool LaunchTcpServer(ITcpServer* server, const std::string& listenIp, const UInt16 listenPort, const int size) override;

        int AddEpoll(int fd, UInt32 events, void* userData);
        void DeleteEpoll(int fd);
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
        int _EpollerFd = INVALID_FD;
        SafeQueue::SpscQueue<NetEvent*> _EventQueue;
        SafeQueue::SpscQueue<NetRequest*> _RequestQueue;
        SpinLock _ObjectsLock;
        std::unordered_set<Tcper*> _Tcpers;
        std::unordered_set<Accepter*> _Acceptors;
    };
}

#endif //__Net_h__
