#include "Foundation.h"

#include "FoundationExport.h"
#include "Http/Http.h"
#include "Interface/IFoundation.h"
#include "Interface/IHttp.h"
#include "Interface/ILog.h"
#include "Interface/IMemory.h"
#include "Interface/IMysql.h"
#include "Interface/INet.h"
#include "Interface/IRedis.h"
#include "Interface/ITaskManager.h"
#include "Interface/ISystem.h"
#include "Interface/ITimeWheel.h"
#include "Interface/IWebSockets.h"
#include "Log/Log.h"
#include "Memory/Memory.h"
#include "Mysql/Mysql.h"
#if defined(_WIN32)
#include "Net/Windows/Net.h"
#elif defined(__APPLE__)
#include "Net/MacOS/Net.h"
#else
#include "Net/Linux/Net.h"
#endif
#include "Redis/RedisClient.h"
#include "Ssh/Ssh.h"
#include "Task/TaskManager.h"
#include "System/System.h"
#include "TimeWheel/TimeWheel.h"
#include "WebSockets/WebSockets.h"

#include <new>

namespace Foundation {
    Foundation::Foundation() {
        try {
            _OwnedComponents[0] = new (std::nothrow) Log();
            _OwnedComponents[1] = new (std::nothrow) TaskManager();
            _OwnedComponents[2] = new (std::nothrow) System();
            _OwnedComponents[3] = new (std::nothrow) Memory();
            _OwnedComponents[4] = new (std::nothrow) Net();
            _OwnedComponents[5] = new (std::nothrow) TimeWheel();
            _OwnedComponents[6] = new (std::nothrow) Http();
            _OwnedComponents[7] = new (std::nothrow) Mysql();
            _OwnedComponents[8] = new (std::nothrow) RedisClient();
            _OwnedComponents[9] = new (std::nothrow) WebSockets();
            _OwnedComponents[10] = new (std::nothrow) Ssh();
        }
        catch (...) {
            for (std::size_t index = s_ComponentCount; index > 0; --index) {
                delete _OwnedComponents[index - 1];
            }
            throw;
        }

        _Log = dynamic_cast<ILog*>(_OwnedComponents[0]);
        _Task = dynamic_cast<ITaskManager*>(_OwnedComponents[1]);
        _System = dynamic_cast<ISystem*>(_OwnedComponents[2]);
        _Memory = dynamic_cast<IMemory*>(_OwnedComponents[3]);
        _Net = dynamic_cast<INet*>(_OwnedComponents[4]);
        _Timer = dynamic_cast<ITimeWheel*>(_OwnedComponents[5]);
        _Http = dynamic_cast<IHttp*>(_OwnedComponents[6]);
        _Mysql = dynamic_cast<IMysql*>(_OwnedComponents[7]);
        _Redis = dynamic_cast<IRedis*>(_OwnedComponents[8]);
        _WebSockets = dynamic_cast<IWebSockets*>(_OwnedComponents[9]);
        _Ssh = dynamic_cast<Ssh*>(_OwnedComponents[10]);
        _Components = {
            _Log,
            _Task,
            _System,
            _Memory,
            _Net,
            _Timer,
            _Http,
            _Mysql,
            _Redis,
            _WebSockets,
            _Ssh
        };
    }

    Foundation::~Foundation() {
        Release();
        for (std::size_t index = s_ComponentCount; index > 0; --index) {
            delete _OwnedComponents[index - 1];
            _OwnedComponents[index - 1] = nullptr;
        }
    }

    bool Foundation::Initialize(int argc, char** args) {
        if (_State != eFoundationState::Created) {
            return false;
        }

        for (int index = 1; args && index < argc; ++index) {
            if (!args[index]) {
                continue;
            }

            const std::string argument(args[index]);
            if (argument.size() <= 2 || argument.compare(0, 2, "--") != 0) {
                continue;
            }

            const std::size_t separator = argument.find('=', 2);
            if (separator == 2) {
                continue;
            }

            if (separator == std::string::npos) {
                _LaunchArgs[argument.substr(2)] = "";
            }
            else {
                _LaunchArgs[argument.substr(2, separator - 2)] =
                    argument.substr(separator + 1);
            }
        }

        _InitializedComponentCount = 0;
        for (IFoundation* component : _Components) {
            if (!component || !component->Initialize(this)) {
                FError("Foundation component %zu failed to initialize", _InitializedComponentCount);
                ReleaseComponents(_InitializedComponentCount);
                _State = eFoundationState::Released;
                return false;
            }
            ++_InitializedComponentCount;
        }

        _State = eFoundationState::Initialized;
        return true;
    }

    bool Foundation::Launch() {
        if (_State != eFoundationState::Initialized) {
            return false;
        }

        for (std::size_t index = 0; index < _InitializedComponentCount; ++index) {
            if (!_Components[index]->Launch(this)) {
                FError("Foundation component %zu failed to launch", index);
                ReleaseComponents(_InitializedComponentCount);
                _State = eFoundationState::Released;
                return false;
            }
        }

        _State = eFoundationState::Launched;
        return true;
    }

    void Foundation::Release() {
        if (_State == eFoundationState::Released) {
            return;
        }
        if (_State == eFoundationState::Created) {
            _State = eFoundationState::Released;
            return;
        }

        ReleaseComponents(_InitializedComponentCount);
        _State = eFoundationState::Released;
    }

    const char* Foundation::GetLaunchArg(const std::string& name) const {
        const auto iter = _LaunchArgs.find(name);
        return iter != _LaunchArgs.end() ? iter->second.c_str() : nullptr;
    }

    INetApi* Foundation::NetApi() const {
        return _Net;
    }

    ISshApi* Foundation::SshApi() const {
        return _Ssh;
    }

    ISystemApi* Foundation::SystemApi() const {
        return _System;
    }

    ITimerApi* Foundation::TimerApi() const {
        return _Timer;
    }

    IHttpApi* Foundation::HttpApi() const {
        return _Http;
    }

    IMemoryApi* Foundation::MemoryApi() const {
        return _Memory;
    }

    IMysqlApi* Foundation::MysqlApi() const {
        return _Mysql;
    }

    IRedisApi* Foundation::RedisApi() const {
        return _Redis;
    }

    ITaskApi* Foundation::TaskApi() const {
        return _Task;
    }

    IWebSocketsApi* Foundation::WebSocketsApi() const {
        return _WebSockets;
    }

    void Foundation::Update() {
        if (_State != eFoundationState::Launched || _Updating) {
            return;
        }

        _Updating = true;
        struct UpdatingGuard final {
            bool& _Updating;
            ~UpdatingGuard() {
                _Updating = false;
            }
        } guard{ _Updating };
        for (std::size_t index = 0; index < _InitializedComponentCount; ++index) {
            _Components[index]->EarlyUpdate(this);
        }
        for (std::size_t index = 0; index < _InitializedComponentCount; ++index) {
            _Components[index]->Update(this);
        }
        for (std::size_t index = 0; index < _InitializedComponentCount; ++index) {
            _Components[index]->LaterUpdate(this);
        }
    }

    void Foundation::ReleaseComponents(std::size_t componentCount) noexcept {
        while (componentCount > 0) {
            --componentCount;
            _Components[componentCount]->Release(this);
        }
        _InitializedComponentCount = 0;
    }

    void Foundation::RegisterLogEvent(const fLogEvent callback) {
        if (_Log) {
            _Log->RegisterLogEvent(callback);
        }
    }

    void Foundation::SetLogTagFunction(const fGetLogTagByLevel function) {
        if (_Log) {
            _Log->SetLogTagFunction(function);
        }
    }

    bool Foundation::SetLogInfo(const std::string& dir, const std::string& name) {
        return _Log && _Log->SetLogInfo(dir, name);
    }

    void Foundation::LogAsync(const int level, const char* content, const bool console, const char* file, const int line) {
        if (_Log) {
            _Log->LogAsync(level, content, console, file, line);
        }
        else {
            FError("%s:%d >> %s", file ? file : "", line, content ? content : "");
        }
    }

    void Foundation::LogSync(const int level, const char* content, const bool console, const char* file, const int line) {
        if (_Log) {
            _Log->LogSync(level, content, console, file, line);
        }
        else {
            FError("%s:%d >> %s", file ? file : "", line, content ? content : "");
        }
    }
}

extern "C" API Foundation::FoundationApi* CALL CreateFoundation() noexcept {
    return new (std::nothrow) Foundation::Foundation();
}

extern "C" API void CALL ReleaseFoundation(Foundation::FoundationApi* foundation) noexcept {
    if (!foundation) {
        return;
    }
    foundation->Release();
    delete foundation;
}
