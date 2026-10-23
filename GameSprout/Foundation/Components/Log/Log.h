#ifndef LOG_H
#define LOG_H

#include "Header.h"

namespace Foundation {
    class Log final : public ILog, public FoundationUtils::SafeThread {
    public:
        virtual ~Log() override {}

        bool Initialize(FoundationApi* foundation) override;
        bool Launch(FoundationApi* foundation) override;
        void Release(FoundationApi* foundation) override;
        void EarlyUpdate(FoundationApi* foundation) override;
        void Update(FoundationApi* foundation) override;
        void LaterUpdate(FoundationApi* foundation) override;

        void RegisterLogEvent(const FoundationApi::fLogEvent callback) override;
        void SetLogTagFunction(const FoundationApi::fGetLogTagByLevel function) override;
        bool SetLogInfo(const std::string& dir, const std::string& name) override;
        void LogAsync(const int level, const char* content, const bool console, const char* file, const int line) override;
        void LogSync(const int level, const char* content, const bool console, const char* file, const int line) override;

        void Run(const int threadIndex, void* const context) override;

    private:
        std::string GetLogLevelTag(const int level) const;
        std::string FormatLog(const LogContent* content) const;
        void QueueLog(const int level, const char* content, const bool console, const char* file, const int line);
        void NotifyLogEvent(const LogContent* content);
        void WriteContent(LogContent* content);

        FoundationApi* _Foundation = nullptr;
        FoundationApi::fGetLogTagByLevel _GetLogTagFunction = nullptr;
        std::vector<FoundationApi::fLogEvent> _LogEvents;

        FoundationUtils::XPool<LogContent> _LogContentPool;
        FoundationUtils::SafeQueue::SpscQueue<LogContent*> _LogContentQueue;
        FoundationUtils::SafeQueue::SpscQueue<LogContent*> _LogContentReleaseQueue;

        FoundationUtils::XFile _SyncLogFile;
        FoundationUtils::SpinLock _SyncLogLock;
        FoundationUtils::XFile _AsyncLogFile;
        mutable FoundationUtils::SpinLock _EventLock;
        std::atomic_bool _Configured = false;
    };
}

#endif // LOG_H
