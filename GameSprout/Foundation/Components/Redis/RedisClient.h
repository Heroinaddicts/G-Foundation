#ifndef __RedisClient_h__
#define __RedisClient_h__

#include "Header.h"

#include <atomic>

namespace Foundation {
    class RedisSession;

    class RedisClient : public IRedis {
    public:
        virtual ~RedisClient() {}

        bool Initialize(FoundationApi* const foundation) override;
        bool Launch(FoundationApi* const foundation) override;
        void Release(FoundationApi* const foundation) override;
        void EarlyUpdate(FoundationApi* const foundation) override;
        void Update(FoundationApi* const foundation) override;
        void LaterUpdate(FoundationApi* const foundation) override;
        void ConnectRedis(const RedisConnectOptions& options, fConnectRedisCallback callback) override;
        void ConnectRedisEx(const RedisConnectOptions& options, fConnectRedisResultCallback callback) override;

        void RegisterSession(RedisSession* session);
        void UnregisterSession(RedisSession* session);
        Int64 NextSessionIndex();

    private:
        FoundationApi* _Foundation = nullptr;
        FoundationUtils::SpinLock _SessionsLock;
        std::set<RedisSession*> _Sessions;
        std::atomic<Int64> _SessionIndexOffset{0};
        std::atomic_bool _Releasing = false;
        bool _Launched = false;
    };
}

#endif //__RedisClient_h__
