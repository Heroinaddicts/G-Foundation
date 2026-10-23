#ifndef __Net_h__
#define __Net_h__

#include "Header.h"

#include <set>
#include <vector>

namespace Foundation {
    class Tcper;

    class Net : public INet {
    public:
        virtual ~Net() {}

        static INet* GetInstance();

        // 通过 IFoundation 继承
        virtual bool Initialize(FoundationApi* const foundation) override;
        virtual bool Launch(FoundationApi* const foundation) override;
        virtual void Release(FoundationApi* const foundation) override;
        virtual void EarlyUpdate(FoundationApi* const foundation) override;
        virtual void Update(FoundationApi* const foundation) override;
        virtual void LaterUpdate(FoundationApi* const foundation) override;

        // 通过 INet 继承
        virtual bool LaunchTcpSession(ITcpSession* session, const std::string& host, const UInt16 port, const int size) override;
        virtual bool LaunchTcpServer(ITcpServer* server, const std::string& ip, const UInt16 port, const int size) override;
        void RetireTcpPipe(Tcper* pipe);
    private:
        friend class Foundation;
        Net() {}

        void ReleaseRetiredTcpPipes();

        std::vector<Tcper*> _RetiredTcpPipes;
    };
}

#endif //__Net_h__
