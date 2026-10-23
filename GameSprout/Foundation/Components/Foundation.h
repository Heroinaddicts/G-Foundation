#ifndef Foundation_H
#define Foundation_H

#include "FoundationApi.h"

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <map>

namespace Foundation {
    inline void FError(const char* format, ...) {
        if (!format) {
            return;
        }

        std::fputs("[Foundation][Error] ", stderr);
        va_list arguments;
        va_start(arguments, format);
        std::vfprintf(stderr, format, arguments);
        va_end(arguments);
        std::fputc('\n', stderr);
        std::fflush(stderr);
    }

    class IFoundation;
    class IHttp;
    class ILog;
    class IMemory;
    class IMysql;
    class INet;
    class IRedis;
    class ITaskManager;
    class ISystem;
    class ITimeWheel;
    class IWebSockets;
    class Ssh;

    class Foundation final : public FoundationApi {
    public:
        Foundation();
        ~Foundation() override;

        Foundation(const Foundation&) = delete;
        Foundation& operator=(const Foundation&) = delete;

        bool Initialize(int argc, char** args) override;
        bool Launch() override;
        void Release() override;

        const char* GetLaunchArg(const std::string& name) const override;

        INetApi* NetApi() const override;
        ISshApi* SshApi() const override;
        ISystemApi* SystemApi() const override;
        ITimerApi* TimerApi() const override;
        IHttpApi* HttpApi() const override;
        IMemoryApi* MemoryApi() const override;
        IMysqlApi* MysqlApi() const override;
        IRedisApi* RedisApi() const override;
        ITaskApi* TaskApi() const override;
        IWebSocketsApi* WebSocketsApi() const override;

        void Update() override;

        void RegisterLogEvent(const fLogEvent callback) override;
        void SetLogTagFunction(const fGetLogTagByLevel function) override;
        bool SetLogInfo(const std::string& dir, const std::string& name) override;
        void LogAsync(const int level, const char* content, const bool console, const char* file, const int line) override;
        void LogSync(const int level, const char* content, const bool console, const char* file, const int line) override;

    private:
        enum class eFoundationState {
            Created,
            Initialized,
            Launched,
            Released
        };

        void ReleaseComponents(std::size_t componentCount) noexcept;

        static constexpr std::size_t s_ComponentCount = 11;

        std::array<IFoundation*, s_ComponentCount> _OwnedComponents{};

        ILog* _Log = nullptr;
        ITaskManager* _Task = nullptr;
        ISystem* _System = nullptr;
        IMemory* _Memory = nullptr;
        INet* _Net = nullptr;
        ITimeWheel* _Timer = nullptr;
        IHttp* _Http = nullptr;
        IMysql* _Mysql = nullptr;
        IRedis* _Redis = nullptr;
        IWebSockets* _WebSockets = nullptr;
        Ssh* _Ssh = nullptr;
        std::array<IFoundation*, s_ComponentCount> _Components{};
        std::size_t _InitializedComponentCount = 0;
        std::map<std::string, std::string> _LaunchArgs;
        eFoundationState _State = eFoundationState::Created;
        bool _Updating = false;
    };
}

#endif //Foundation_H
