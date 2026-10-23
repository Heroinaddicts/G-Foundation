#ifndef MYSQL_SESSION_H
#define MYSQL_SESSION_H

#include "Header.h"

namespace Foundation {
    class MysqlSession final : public IMysqlSession, public FoundationUtils::SafeThread {
    public:
        virtual ~MysqlSession() override;

        static MysqlSession* Create(
            FoundationApi* foundation,
            MYSQL* connection);
        static void Release(MysqlSession* session);

        void Query(const std::string& sql, const fQueryCallback& callback) override;
        void QueryEx(const std::string& sql, const fMysqlQueryCallback& callback) override;
        void Execute(const std::string& sql, const fMysqlQueryCallback& callback) override;
        void Begin(const fMysqlQueryCallback& callback) override;
        void Commit(const fMysqlQueryCallback& callback) override;
        void Rollback(const fMysqlQueryCallback& callback) override;
        std::string EscapeString(const std::string& input) const override;
        bool IsReady() const override;
        void Close() override;

        bool Closed() const {
            return _Closed.load(std::memory_order_acquire);
        }
        void Update(bool all = false);
        void Run(const int threadIndex, void* const context) override;

    private:
        MysqlSession(
            FoundationApi* foundation,
            MYSQL* connection);

        void PushQuery(const std::string& sql, const fQueryCallback& callback, const fMysqlQueryCallback& callbackEx);
        void Deal();
        void FillResultInfo(QueryInfo* query, ResultInfo* resultInfo);

        FoundationApi* _Foundation = nullptr;
        MYSQL* _Connection = nullptr;
        mutable FoundationUtils::SpinLock _ConnectionLock;
        FoundationUtils::SafeQueue::SpscQueue<QueryInfo*, 81920> _QueryQueue;
        FoundationUtils::SafeQueue::SpscQueue<ResultInfo*, 81920> _ResultQueue;
        FoundationUtils::SpinLock _QueryQueueLock;
        std::atomic<bool> _Closed{ false };
        mutable std::atomic<bool> _ConnectionHealthy{ true };
        mutable std::atomic<bool> _QueryExecuting{ false };
    };
}

#endif // MYSQL_SESSION_H
