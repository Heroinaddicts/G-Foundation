#ifndef __RedisSession_h__
#define __RedisSession_h__

#include "Header.h"

namespace Foundation {
    class RedisSubscriber;
    class RedisClient;

    class RedisSession : public IRedisSession {
    public:
        virtual ~RedisSession() {}

        RedisSession(RedisClient* owner, FoundationApi* foundation, const RedisConnectOptions& options, redisContext* con);

        void AsyncGet(const std::string& key, const fStringResult& callback) override;
        void AsyncSet(const std::string& key, const std::string& value, const fBoolResult& callback) override;

        void AsyncSetWhenNotExists(const std::string& key, const std::string& value, const fBoolResult& callback) override;
        void AsyncCAS(const std::string& key, const std::string& compare, const std::string& value, const fBoolResult& callback) override;

        void AsyncGetEx(const std::string& key, const fRedisResult& callback) override;
        void AsyncDel(const std::string& key, const fRedisResult& callback) override;
        void AsyncExists(const std::string& key, const fRedisResult& callback) override;
        void AsyncExpire(const std::string& key, const Int64 seconds, const fRedisResult& callback) override;
        void AsyncTTL(const std::string& key, const fRedisResult& callback) override;
        void AsyncPersist(const std::string& key, const fRedisResult& callback) override;
        void AsyncEval(const std::string& script, const std::vector<std::string>& keys, const std::vector<std::string>& args, const fRedisResult& callback) override;
        void AsyncRawCommand(const std::vector<std::string>& command, const fRedisResult& callback) override;

        void AsyncSubscribe(const std::string& channel, const fRedisMessageCallback& messageCallback, const fRedisResult& callback) override;
        void AsyncUnsubscribe(const std::string& channel, const fRedisResult& callback) override;

        bool IsReady() const override;
        void Close() override;

    private:
        void ExecuteCommand(const std::vector<std::string>& command, const fRedisResult& callback);
        bool BeginCommand(RedisResult& result);
        void FinishCommand();
        void ReleaseClosedSession();

    private:
        RedisClient* _Owner;
        FoundationApi* _Foundation;
        const RedisConnectOptions _Options;
        mutable FoundationUtils::SpinLock _Lock;
        int _CurrentOptCount;
        std::map<std::string, RedisSubscriber*> _Subscribers;

    private:
        redisContext* _Con;
        const Int64 _Index;

        std::atomic_bool _IsClosed{false};
        std::atomic_bool _ConnectionHealthy{false};
        std::atomic<std::intptr_t> _SocketDescriptor{-1};
    };
}

#endif //__RedisSession_h__
