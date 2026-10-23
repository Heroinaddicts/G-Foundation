#include "Log.h"

#include <cstdio>

namespace Foundation {
    bool Log::Initialize(FoundationApi* foundation) {
        _Foundation = foundation;
        return _Foundation != nullptr;
    }

    bool Log::Launch(FoundationApi* foundation) {
        return foundation == _Foundation;
    }

    void Log::Release(FoundationApi* foundation) {
        RequestStop();
        Wait();

        LogContent* content = nullptr;
        while (_LogContentQueue.Pull(content)) {
            if (_Configured.load()) {
                WriteContent(content);
                NotifyLogEvent(content);
            }
            XPOOL_RELEASE(_LogContentPool, content);
        }
        while (_LogContentReleaseQueue.Pull(content)) {
            if (_Configured.load()) {
                NotifyLogEvent(content);
            }
            XPOOL_RELEASE(_LogContentPool, content);
        }
        if (_Configured.load()) {
            _AsyncLogFile.Save();
        }
        {
            AUTO_LOCK(_SyncLogLock);
            if (_Configured.load()) {
                _SyncLogFile.Save();
            }
        }

        _AsyncLogFile.Close();
        _SyncLogFile.Close();
        {
            AUTO_LOCK(_EventLock);
            _LogEvents.clear();
            _GetLogTagFunction = nullptr;
        }

        _Configured.store(false);
        _Foundation = nullptr;
    }

    void Log::EarlyUpdate(FoundationApi* foundation) {
    }

    void Log::Update(FoundationApi* foundation) {
    }

    void Log::LaterUpdate(FoundationApi* foundation) {
        if (!_Foundation) {
            return;
        }

        const UInt64 beginTick = FoundationUtils::SafeSystem::Time::GetMicroSecond();
        LogContent* content = nullptr;
        while (_LogContentReleaseQueue.Pull(content)) {
            NotifyLogEvent(content);
            XPOOL_RELEASE(_LogContentPool, content);
            if (FoundationUtils::SafeSystem::Time::GetMicroSecond() - beginTick > 1000) {
                break;
            }
        }
    }

    void Log::RegisterLogEvent(const FoundationApi::fLogEvent callback) {
        if (!callback) {
            return;
        }
        AUTO_LOCK(_EventLock);
        _LogEvents.push_back(callback);
    }

    void Log::SetLogTagFunction(const FoundationApi::fGetLogTagByLevel function) {
        _GetLogTagFunction = function;
    }

    bool Log::SetLogInfo(const std::string& dir, const std::string& name) {
        if (!_Foundation || dir.empty() || name.empty()) {
            return false;
        }
        if (_Configured.load()) {
            return true;
        }

        if (!FoundationUtils::SafeSystem::File::FolderExists(dir) && !FoundationUtils::SafeSystem::File::CreateFolder(dir)) {
            return false;
        }

        const std::string processId = FoundationUtils::SafeString::UInt64ToString(FoundationUtils::SafeSystem::Process::GetCurrentProcessID());
        const std::string syncLogFilePath = dir + "/" + name + "-sync-" + processId + ".log";
        const std::string asyncLogFilePath = dir + "/" + name + "-async-" + processId + ".log";
        if (!_SyncLogFile.Open(syncLogFilePath)) {
            return false;
        }
        if (!_AsyncLogFile.Open(asyncLogFilePath)) {
            _SyncLogFile.Close();
            return false;
        }

        _Configured.store(true);
        if (!Start()) {
            _Configured.store(false);
            _AsyncLogFile.Close();
            _SyncLogFile.Close();
            return false;
        }

        return true;
    }

    void Log::LogAsync(const int level, const char* content, const bool console, const char* file, const int line) {
        QueueLog(level, content, console, file, line);
    }

    void Log::LogSync(const int level, const char* content, const bool console, const char* file, const int line) {
        if (!_Configured.load()) {
            LogAsync(level, content, console, file, line);
            return;
        }

        const UInt64 tick = FoundationUtils::SafeSystem::Time::GetMilliSecond();
        LogContent logContent(level, content ? content : "", console, file ? file : "", line, tick);
        const std::string formattedContent = FormatLog(&logContent);

        {
            AUTO_LOCK(_SyncLogLock);
            if (_Configured.load()) {
                _SyncLogFile << formattedContent;
                _SyncLogFile.Save();
            }
        }
        if (console) {
            std::printf("%s", formattedContent.c_str());
        }
        NotifyLogEvent(&logContent);
    }

    void Log::Run(const int, void* const) {
        if (!_Foundation) {
            return;
        }

        int pendingCount = 0;
        UInt64 lastSaveTick = FoundationUtils::SafeSystem::Time::GetMilliSecond();
        while (!IsStopRequested()) {
            LogContent* content = nullptr;
            _LogContentQueue.Pull(content);
            if (content) {
                WriteContent(content);
                _LogContentReleaseQueue.Push(content);
                ++pendingCount;
            }
            else {
                FoundationUtils::SafeSystem::Time::MillisecondSleep(2);
            }

            const UInt64 currentTick = FoundationUtils::SafeSystem::Time::GetMilliSecond();
            if (pendingCount > 0 && (pendingCount >= 50 || currentTick - lastSaveTick >= 50)) {
                _AsyncLogFile.Save();
                lastSaveTick = currentTick;
                pendingCount = 0;
            }
        }
        _AsyncLogFile.Save();
    }

    void Log::QueueLog(const int level, const char* content, const bool console, const char* file, const int line) {
        const UInt64 tick = FoundationUtils::SafeSystem::Time::GetMilliSecond();
        LogContent* logContent = XPOOL_CREATE(_LogContentPool, level, content ? content : "", console, file ? file : "", line, tick);

        if (!logContent) {
            return;
        }

        _LogContentQueue.Push(logContent);
    }

    std::string Log::GetLogLevelTag(const int level) const {
        return _GetLogTagFunction ? _GetLogTagFunction(level) : "[Unknown]";
    }

    std::string Log::FormatLog(const LogContent* content) const {
        if (!content) {
            return "";
        }

        const std::string time = FoundationUtils::SafeSystem::Time::GetTimeStringByMillisecond(static_cast<Int64>(content->_Tick));
        const std::string line = FoundationUtils::SafeString::Int32ToString(content->_Line);
        return time + " " + GetLogLevelTag(content->_Level) + " " + content->_File + ":" + line
            + " >> " + content->_Content + "\n";
    }

    void Log::NotifyLogEvent(const LogContent* content) {
        if (!content || !_Foundation) {
            return;
        }

        std::vector<FoundationApi::fLogEvent> callbacks;
        {
            AUTO_LOCK(_EventLock);
            callbacks = _LogEvents;
        }
        for (FoundationApi::fLogEvent callback : callbacks) {
            if (callback) {
                callback(_Foundation, content->_Level, content->_Content.c_str(), content->_Console, content->_File.c_str(), content->_Line);
            }
        }
    }

    void Log::WriteContent(LogContent* content) {
        if (!content) {
            return;
        }
        const std::string formattedContent = FormatLog(content);
        if (_Configured.load()) {
            _AsyncLogFile << formattedContent;
        }
        if (content->_Console) {
            std::printf("%s", formattedContent.c_str());
        }
    }
}
