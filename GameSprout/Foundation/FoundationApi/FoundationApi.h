#ifndef FoundationAPI_H
#define FoundationAPI_H

#include "INetApi.h"
#include "ISshApi.h"
#include "ISystemApi.h"
#include "ITimerApi.h"
#include "IHttpApi.h"
#include "IMemoryApi.h"
#include "IMysqlApi.h"
#include "IRedisApi.h"
#include "ITaskApi.h"
#include "IWebSocketsApi.h"

#include <cstdint>
#include <string>

#define UsingFoundation using namespace Foundation

namespace Foundation {
    inline constexpr int32_t CustomLogLevelBegin = 0x00010000;

    // Values below CustomLogLevelBegin are reserved by Foundation.
    enum class eLogLevel : int32_t {
        FoundationDebug,
        FoundationTrace,
        FoundationError,

        Debug,
        Trace,
        Error,
    };

    class FoundationApi {
    public:
        virtual ~FoundationApi() {}

        virtual bool Initialize(int argc, char **args) = 0;
        virtual bool Launch() = 0;
        virtual void Release() = 0;

        virtual const char * GetLaunchArg(const std::string & name) const = 0;

        virtual INetApi* NetApi() const = 0;
        virtual ISshApi* SshApi() const = 0;
        virtual ISystemApi* SystemApi() const = 0;
        virtual ITimerApi* TimerApi() const = 0;
        virtual IHttpApi* HttpApi() const = 0;
        virtual IMemoryApi* MemoryApi() const = 0;
        virtual IMysqlApi* MysqlApi() const = 0;
        virtual IRedisApi* RedisApi() const = 0;
        virtual ITaskApi* TaskApi() const = 0;
        virtual IWebSocketsApi* WebSocketsApi() const = 0;

        virtual void Update() = 0;


        using fLogEvent = void (*)(FoundationApi* const, const int, const char*, const bool, const char*, const int);
        using fGetLogTagByLevel = std::string(*)(const int level);

        virtual void RegisterLogEvent(const fLogEvent callback) = 0;
        // Startup-only configuration. Call once after Initialize() and before SetLogInfo().
        virtual void SetLogTagFunction(const fGetLogTagByLevel function) = 0;

        virtual bool SetLogInfo(const std::string& dir, const std::string& name) = 0;
        virtual void LogAsync(const int level, const char* content, const bool console, const char* file, const int line) = 0;
        virtual void LogSync(const int level, const char* content, const bool console, const char* file, const int line) = 0;

    };
}

#endif // FoundationAPI_H
