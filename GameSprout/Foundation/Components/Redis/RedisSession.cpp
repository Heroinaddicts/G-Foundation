#include "RedisSession.h"
#include "RedisClient.h"

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <sys/select.h>
#include <sys/socket.h>
#endif

namespace Foundation {
    RedisSession::RedisSession(
        RedisClient* owner,
        FoundationApi* foundation,
        const RedisConnectOptions& options,
        redisContext* con)
        : _Owner(owner),
          _Foundation(foundation),
          _Options(options),
          _CurrentOptCount(0),
          _Con(con),
          _Index(owner ? owner->NextSessionIndex() : -1),
          _ConnectionHealthy(con != nullptr),
          _SocketDescriptor(con ? static_cast<std::intptr_t>(con->fd) : -1) {
    }

    static Int64 RedisCodeToInt(const eRedisCode code) {
        return static_cast<Int64>(code);
    }

    static RedisResult MakeRedisResult(const eRedisCode code, const std::string& error) {
        RedisResult result;
        result._Code = code;
        result._Error = error;
        return result;
    }

    static timeval RedisTimeout(const UInt32 milliseconds) {
        timeval timeout{};
        timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(milliseconds / 1000);
        timeout.tv_usec = static_cast<decltype(timeout.tv_usec)>((milliseconds % 1000) * 1000);
        return timeout;
    }

    static std::string ReplyString(const redisReply* const reply) {
        if (!reply || !reply->str)
            return "";

        return std::string(reply->str, reply->len);
    }

    static eRedisValueType ConvertRedisType(const int type) {
        switch (type) {
        case REDIS_REPLY_STRING:
        case REDIS_REPLY_VERB:
        case REDIS_REPLY_BIGNUM:
            return eRedisValueType::String;
        case REDIS_REPLY_ARRAY:
        case REDIS_REPLY_PUSH:
        case REDIS_REPLY_ATTR:
            return eRedisValueType::Array;
        case REDIS_REPLY_INTEGER:
            return eRedisValueType::Integer;
        case REDIS_REPLY_NIL:
            return eRedisValueType::Nil;
        case REDIS_REPLY_STATUS:
            return eRedisValueType::Status;
        case REDIS_REPLY_ERROR:
            return eRedisValueType::Error;
        case REDIS_REPLY_DOUBLE:
            return eRedisValueType::Double;
        case REDIS_REPLY_BOOL:
            return eRedisValueType::Bool;
        case REDIS_REPLY_MAP:
            return eRedisValueType::Map;
        case REDIS_REPLY_SET:
            return eRedisValueType::Set;
        default:
            return eRedisValueType::None;
        }
    }

    static RedisValue ConvertRedisReply(const redisReply* const reply) {
        RedisValue value;
        if (!reply)
            return value;

        value._Type = ConvertRedisType(reply->type);
        switch (reply->type) {
        case REDIS_REPLY_STRING:
        case REDIS_REPLY_STATUS:
        case REDIS_REPLY_ERROR:
        case REDIS_REPLY_VERB:
        case REDIS_REPLY_BIGNUM:
            value._String = ReplyString(reply);
            break;
        case REDIS_REPLY_INTEGER:
            value._Integer = reply->integer;
            break;
        case REDIS_REPLY_DOUBLE:
            value._Double = reply->dval;
            value._String = ReplyString(reply);
            break;
        case REDIS_REPLY_BOOL:
            value._Boolean = reply->integer != 0;
            value._Integer = reply->integer;
            break;
        case REDIS_REPLY_ARRAY:
        case REDIS_REPLY_MAP:
        case REDIS_REPLY_SET:
        case REDIS_REPLY_ATTR:
        case REDIS_REPLY_PUSH:
            value._Children.reserve(reply->elements);
            for (size_t i = 0; i < reply->elements; ++i) {
                value._Children.push_back(ConvertRedisReply(reply->element[i]));
            }
            break;
        case REDIS_REPLY_NIL:
        default:
            break;
        }

        return value;
    }

    static bool DispatchRedisMessage(
        FoundationApi* foundation,
        const UInt64 serialKey,
        const fRedisMessageCallback& callback,
        const RedisMessage& message) {
        if (!foundation || !foundation->TaskApi() || !callback)
            return false;

        TaskSubmitOptions options;
        options._OwnerId = FoundationTaskOwnerId;
        options._ExecutionType = eTaskExecutionType::Serialized;
        options._SerialKey = serialKey;
        const TaskSubmitResult submitResult = foundation->TaskApi()->SubmitTask(options,
            [=](OUT Int64& code, ITaskContext& context) {
                context.SetOwned(txnew RedisMessage(message));
                code = RedisCodeToInt(eRedisCode::Success);
                return true;
            },
            [=](const TaskCompletion&, const bool succeed, const ITaskContext& context) {
                const RedisMessage* const copiedMessage = context.Get<RedisMessage>();
                if (succeed && copiedMessage) {
                    callback(*copiedMessage);
                }
            }
        );
        return submitResult.Accepted();
    }

    struct RedisSubscribeContext {
        RedisSubscriber* _Subscriber = nullptr;
        RedisResult _Result;
    };

    class RedisSubscriber : public FoundationUtils::SafeThread {
    public:
        RedisSubscriber(FoundationApi* foundation, const RedisConnectOptions& options, const UInt64 dispatchSerialKey, const std::string& channel, const fRedisMessageCallback& callback)
            : _Foundation(foundation), _Options(options), _DispatchSerialKey(dispatchSerialKey), _Channel(channel), _Callback(callback), _Con(nullptr) {
        }

        ~RedisSubscriber() {
            Stop();
        }

        RedisResult Initialize() {
            const timeval connectTimeout = RedisTimeout(_Options._ConnectTimeoutMilliseconds);
            _Con = redisConnectWithTimeout(_Options._Host.c_str(), _Options._Port, connectTimeout);
            if (nullptr == _Con)
                return MakeRedisResult(eRedisCode::CommandError, "redis connect failed: can't allocate redis context");

            if (_Con->err) {
                RedisResult result = MakeRedisResult(eRedisCode::CommandError, _Con->errstr);
                redisFree(_Con);
                _Con = nullptr;
                return result;
            }

            const timeval operationTimeout = RedisTimeout(_Options._OperationTimeoutMilliseconds);
            if (redisSetTimeout(_Con, operationTimeout) != REDIS_OK) {
                return MakeRedisResult(
                    eRedisCode::CommandError,
                    _Con->errstr[0] != '\0'
                        ? _Con->errstr
                        : "can't configure redis operation timeout");
            }

            if (!_Options._Password.empty()) {
                std::vector<std::string> auth = { "AUTH", _Options._Password };
                RedisResult authResult = ExecuteImmediate(auth);
                if (!authResult.Success())
                    return authResult;
            }

            std::vector<std::string> subscribe = { "SUBSCRIBE", _Channel };
            RedisResult subscribeResult = ExecuteImmediate(subscribe);
            if (!subscribeResult.Success())
                return subscribeResult;

            return subscribeResult;
        }

        bool Start() {
            if (!_Foundation || RunningThreadCount() != 0) {
                return false;
            }
            _Failed.store(false, std::memory_order_release);
            if (!FoundationUtils::SafeThread::Start()) {
                _Failed.store(true, std::memory_order_release);
                return false;
            }
            return true;
        }

        bool IsActive() const {
            return !_Failed.load(std::memory_order_acquire)
                && RunningThreadCount() != 0;
        }

        void Stop() {
            RequestStop();
            if (_Con && _Con->fd != REDIS_INVALID_FD) {
#if defined(_WIN32)
                shutdown(_Con->fd, SD_BOTH);
#else
                ::shutdown(_Con->fd, SHUT_RDWR);
#endif
            }
            Wait();

            if (_Con) {
                redisFree(_Con);
                _Con = nullptr;
            }
        }

        void Run(const int, void* const) override {
            bool failed = false;
            while (!IsStopRequested()) {
                fd_set readSet;
                FD_ZERO(&readSet);
                FD_SET(_Con->fd, &readSet);

                timeval timeout;
                timeout.tv_sec = 0;
                timeout.tv_usec = 200000;

#if defined(_WIN32)
                int ret = select(0, &readSet, nullptr, nullptr, &timeout);
#else
                int ret = select(_Con->fd + 1, &readSet, nullptr, nullptr, &timeout);
#endif
                if (ret == 0)
                    continue;

                if (ret < 0) {
                    failed = !IsStopRequested();
                    break;
                }

                void* rawReply = nullptr;
                if (redisGetReply(_Con, &rawReply) != REDIS_OK) {
                    failed = !IsStopRequested();
                    break;
                }

                redisReply* reply = (redisReply*)rawReply;
                if (!DispatchMessage(reply)) {
                    failed = !IsStopRequested();
                }
                freeReplyObject(reply);
                if (failed)
                    break;
            }
            _Failed.store(failed, std::memory_order_release);
        }

    private:
        RedisResult ExecuteImmediate(const std::vector<std::string>& command) {
            if (command.empty())
                return MakeRedisResult(eRedisCode::InvalidArgument, "redis command is empty");

            std::vector<const char*> argv;
            std::vector<size_t> argvlen;
            argv.reserve(command.size());
            argvlen.reserve(command.size());
            for (const std::string& arg : command) {
                argv.push_back(arg.data());
                argvlen.push_back(arg.size());
            }

            redisReply* reply = (redisReply*)redisCommandArgv(_Con, (int)argv.size(), argv.data(), argvlen.data());
            if (nullptr == reply)
                return MakeRedisResult(eRedisCode::CommandError, _Con ? _Con->errstr : "redis command failed");

            RedisResult result;
            result._Value = ConvertRedisReply(reply);
            if (reply->type == REDIS_REPLY_ERROR) {
                result._Code = eRedisCode::ReplyError;
                result._Error = ReplyString(reply);
            }

            freeReplyObject(reply);
            return result;
        }

        bool DispatchMessage(const redisReply* const reply) {
            if (!reply || reply->type != REDIS_REPLY_ARRAY || reply->elements < 3)
                return true;

            const redisReply* type = reply->element[0];
            if (!type || type->type != REDIS_REPLY_STRING)
                return true;

            std::string messageType = ReplyString(type);
            if (messageType != "message")
                return true;

            RedisMessage message;
            message._Channel = ReplyString(reply->element[1]);
            message._Data = ReplyString(reply->element[2]);
            return DispatchRedisMessage(
                _Foundation,
                _DispatchSerialKey,
                _Callback,
                message);
        }

    private:
        FoundationApi* _Foundation;
        const RedisConnectOptions _Options;
        const UInt64 _DispatchSerialKey;
        const std::string _Channel;
        fRedisMessageCallback _Callback;
        redisContext* _Con;
        std::atomic_bool _Failed{ true };
    };

    bool RedisSession::BeginCommand(RedisResult& result) {
        AUTO_LOCK(_Lock);
        if (_IsClosed.load(std::memory_order_acquire) || nullptr == _Con) {
            result = MakeRedisResult(eRedisCode::Closed, "redis session closed");
            return false;
        }

        ++_CurrentOptCount;
        return true;
    }

    void RedisSession::FinishCommand() {
        bool release = false;
        {
            AUTO_LOCK(_Lock);
            --_CurrentOptCount;
            release = _IsClosed.load(std::memory_order_acquire)
                && _CurrentOptCount == 0;
        }

        if (release)
            ReleaseClosedSession();
    }

    void RedisSession::ReleaseClosedSession() {
        std::vector<RedisSubscriber*> subscribers;
        redisContext* con = nullptr;
        {
            AUTO_LOCK(_Lock);
            for (auto itor = _Subscribers.begin(); itor != _Subscribers.end(); ++itor) {
                subscribers.push_back(itor->second);
            }
            _Subscribers.clear();
            con = _Con;
            _Con = nullptr;
            _SocketDescriptor.store(-1, std::memory_order_release);
        }

        for (RedisSubscriber* subscriber : subscribers) {
            subscriber->Stop();
            txdel subscriber;
        }

        if (con)
            redisFree(con);

        if (_Owner) {
            _Owner->UnregisterSession(this);
        }
        txdel this;
    }

    void RedisSession::ExecuteCommand(const std::vector<std::string>& command, const fRedisResult& callback) {
        RedisResult beginResult;
        if (!BeginCommand(beginResult)) {
            if (callback)
                callback(beginResult);
            return;
        }

        if (!_Foundation || !_Foundation->TaskApi()) {
            FinishCommand();
            if (callback) {
                callback(MakeRedisResult(eRedisCode::Closed, "redis foundation is not available"));
            }
            return;
        }
        TaskSubmitOptions options;
        options._OwnerId = FoundationTaskOwnerId;
        options._ExecutionType = eTaskExecutionType::Serialized;
        options._SerialKey = static_cast<UInt64>(_Index) + 1;
        const TaskSubmitResult submitResult = _Foundation->TaskApi()->SubmitTask(options,
            [=](OUT Int64& code, ITaskContext& context) {
                RedisResult* result = txnew RedisResult();
                context.SetOwned(result);
                if (command.empty()) {
                    *result = MakeRedisResult(eRedisCode::InvalidArgument, "redis command is empty");
                    code = RedisCodeToInt(result->_Code);
                    return false;
                }

                std::vector<const char*> argv;
                std::vector<size_t> argvlen;
                argv.reserve(command.size());
                argvlen.reserve(command.size());
                for (const std::string& arg : command) {
                    argv.push_back(arg.data());
                    argvlen.push_back(arg.size());
                }

                {
                    AUTO_LOCK(_Lock);
                    if (_IsClosed.load(std::memory_order_acquire)
                        || nullptr == _Con) {
                        *result = MakeRedisResult(eRedisCode::Closed, "redis session closed");
                        code = RedisCodeToInt(result->_Code);
                        return false;
                    }

                    redisReply* reply = (redisReply*)redisCommandArgv(_Con, (int)argv.size(), argv.data(), argvlen.data());
                    if (nullptr == reply) {
                        *result = MakeRedisResult(eRedisCode::CommandError, _Con ? _Con->errstr : "redis command failed");
                        _ConnectionHealthy.store(false, std::memory_order_release);
                        code = RedisCodeToInt(result->_Code);
                        return false;
                    }

                    result->_Value = ConvertRedisReply(reply);
                    if (reply->type == REDIS_REPLY_ERROR) {
                        result->_Code = eRedisCode::ReplyError;
                        result->_Error = ReplyString(reply);
                    }
                    if (_Con->err != 0) {
                        _ConnectionHealthy.store(false, std::memory_order_release);
                    }

                    freeReplyObject(reply);
                }

                code = RedisCodeToInt(result->_Code);
                return result->Success();
            },
            [=](const TaskCompletion&, const bool, const ITaskContext& context) {
                const RedisResult* const result = context.Get<RedisResult>();
                FinishCommand();
                if (callback) {
                    if (result) {
                        callback(*result);
                    }
                    else {
                        callback(MakeRedisResult(
                            eRedisCode::Closed,
                            context.IsCancellationRequested()
                                ? "redis command task was cancelled"
                                : "redis command task failed"
                        ));
                    }
                }
            }
        );
        if (!submitResult.Accepted()) {
            FinishCommand();
            if (callback) {
                callback(MakeRedisResult(eRedisCode::Closed, "redis task system is unavailable"));
            }
        }
    }

    void RedisSession::AsyncGet(const std::string& key, const fStringResult& callback) {
        AsyncGetEx(key,
            [=](const RedisResult& result) {
                if (!callback)
                    return;

                if (result.Success() && result._Value._Type == eRedisValueType::String) {
                    callback(true, result._Value._String.c_str());
                }
                else if (result.Success() && result._Value._Type == eRedisValueType::Nil) {
                    callback(false, "");
                }
                else {
                    callback(false, "");
                }
            }
        );
    }

    void RedisSession::AsyncSet(const std::string& key, const std::string& value, const fBoolResult& callback) {
        AsyncRawCommand({ "SET", key, value },
            [=](const RedisResult& result) {
                if (callback)
                    callback(result.Success() && result._Value._Type == eRedisValueType::Status);
            }
        );
    }

    void RedisSession::AsyncSetWhenNotExists(const std::string& key, const std::string& value, const fBoolResult& callback) {
        AsyncRawCommand({ "SET", key, value, "NX" },
            [=](const RedisResult& result) {
                if (callback)
                    callback(result.Success() && result._Value._Type == eRedisValueType::Status);
            }
        );
    }

    void RedisSession::AsyncCAS(const std::string& key, const std::string& compare, const std::string& value, const fBoolResult& callback) {
        const char* script =
            "if redis.call('GET', KEYS[1]) == ARGV[1] then "
            "redis.call('SET', KEYS[1], ARGV[2]) "
            "return 1 "
            "else "
            "return 0 "
            "end";

        AsyncEval(script, { key }, { compare, value },
            [=](const RedisResult& result) {
                if (callback)
                    callback(result.Success() && result._Value._Type == eRedisValueType::Integer && result._Value._Integer == 1);
            }
        );
    }

    void RedisSession::AsyncGetEx(const std::string& key, const fRedisResult& callback) {
        AsyncRawCommand({ "GET", key }, callback);
    }

    void RedisSession::AsyncDel(const std::string& key, const fRedisResult& callback) {
        AsyncRawCommand({ "DEL", key }, callback);
    }

    void RedisSession::AsyncExists(const std::string& key, const fRedisResult& callback) {
        AsyncRawCommand({ "EXISTS", key }, callback);
    }

    void RedisSession::AsyncExpire(const std::string& key, const Int64 seconds, const fRedisResult& callback) {
        AsyncRawCommand({ "EXPIRE", key, std::to_string(seconds) }, callback);
    }

    void RedisSession::AsyncTTL(const std::string& key, const fRedisResult& callback) {
        AsyncRawCommand({ "TTL", key }, callback);
    }

    void RedisSession::AsyncPersist(const std::string& key, const fRedisResult& callback) {
        AsyncRawCommand({ "PERSIST", key }, callback);
    }

    void RedisSession::AsyncEval(const std::string& script, const std::vector<std::string>& keys, const std::vector<std::string>& args, const fRedisResult& callback) {
        std::vector<std::string> command;
        command.reserve(3 + keys.size() + args.size());
        command.push_back("EVAL");
        command.push_back(script);
        command.push_back(std::to_string(keys.size()));
        for (const std::string& key : keys) {
            command.push_back(key);
        }
        for (const std::string& arg : args) {
            command.push_back(arg);
        }

        AsyncRawCommand(command, callback);
    }

    void RedisSession::AsyncRawCommand(const std::vector<std::string>& command, const fRedisResult& callback) {
        ExecuteCommand(command, callback);
    }

    void RedisSession::AsyncSubscribe(const std::string& channel, const fRedisMessageCallback& messageCallback, const fRedisResult& callback) {
        if (channel.empty() || !messageCallback) {
            if (callback)
                callback(MakeRedisResult(
                    eRedisCode::InvalidArgument,
                    channel.empty()
                        ? "redis subscribe channel is empty"
                        : "redis subscribe message callback is empty"));
            return;
        }

        RedisResult beginResult;
        if (!BeginCommand(beginResult)) {
            if (callback)
                callback(beginResult);
            return;
        }

        bool alreadySubscribed = false;
        RedisSubscriber* staleSubscriber = nullptr;
        {
            AUTO_LOCK(_Lock);
            const auto itor = _Subscribers.find(channel);
            if (itor != _Subscribers.end()) {
                if (itor->second && itor->second->IsActive()) {
                    alreadySubscribed = true;
                }
                else {
                    staleSubscriber = itor->second;
                    _Subscribers.erase(itor);
                }
            }
        }
        if (staleSubscriber) {
            staleSubscriber->Stop();
            txdel staleSubscriber;
        }
        if (alreadySubscribed) {
            FinishCommand();
            if (callback)
                callback(RedisResult());
            return;
        }

        if (!_Foundation || !_Foundation->TaskApi()) {
            FinishCommand();
            if (callback) {
                callback(MakeRedisResult(eRedisCode::Closed, "redis foundation is not available"));
            }
            return;
        }
        TaskSubmitOptions options;
        options._OwnerId = FoundationTaskOwnerId;
        options._ExecutionType = eTaskExecutionType::Serialized;
        options._SerialKey = static_cast<UInt64>(_Index) + 1;
        const TaskSubmitResult submitResult = _Foundation->TaskApi()->SubmitTask(options,
            [=](OUT Int64& code, ITaskContext& context) {
                RedisSubscribeContext* info = txnew RedisSubscribeContext();
                context.SetOwned(info);
                info->_Subscriber = txnew RedisSubscriber(
                    _Foundation,
                    _Options,
                    (static_cast<UInt64>(_Index) + 1) | (UInt64(1) << 63),
                    channel,
                    messageCallback);
                info->_Result = info->_Subscriber->Initialize();
                if (!info->_Result.Success()) {
                    code = RedisCodeToInt(info->_Result._Code);
                    return false;
                }

                if (!info->_Subscriber->Start()) {
                    info->_Result = MakeRedisResult(eRedisCode::CommandError, "redis subscribe thread start failed");
                    code = RedisCodeToInt(info->_Result._Code);
                    return false;
                }

                code = RedisCodeToInt(eRedisCode::Success);
                return true;
            },
            [=](const TaskCompletion&, const bool succeed, const ITaskContext& context) {
                RedisSubscribeContext* info = context.Get<RedisSubscribeContext>();
                if (succeed && info && info->_Result.Success()) {
                    AUTO_LOCK(_Lock);
                    if (_IsClosed.load(std::memory_order_acquire)) {
                        info->_Result = MakeRedisResult(eRedisCode::Closed, "redis session closed");
                    }
                    else if (_Subscribers.find(channel) == _Subscribers.end()) {
                        _Subscribers[channel] = info->_Subscriber;
                        info->_Subscriber = nullptr;
                    }
                }

                if (info && info->_Subscriber) {
                    info->_Subscriber->Stop();
                    txdel info->_Subscriber;
                    info->_Subscriber = nullptr;
                }

                FinishCommand();
                if (callback) {
                    if (info) {
                        callback(info->_Result);
                    }
                    else {
                        callback(MakeRedisResult(
                            eRedisCode::Closed,
                            context.IsCancellationRequested()
                                ? "redis subscribe task was cancelled"
                                : "redis subscribe task failed"
                        ));
                    }
                }
            }
        );
        if (!submitResult.Accepted()) {
            FinishCommand();
            if (callback) {
                callback(MakeRedisResult(eRedisCode::Closed, "redis task system is unavailable"));
            }
        }
    }

    void RedisSession::AsyncUnsubscribe(const std::string& channel, const fRedisResult& callback) {
        RedisSubscriber* subscriber = nullptr;
        {
            AUTO_LOCK(_Lock);
            auto itor = _Subscribers.find(channel);
            if (itor != _Subscribers.end()) {
                subscriber = itor->second;
                _Subscribers.erase(itor);
            }
        }

        if (nullptr == subscriber) {
            if (callback)
                callback(MakeRedisResult(eRedisCode::InvalidArgument, "redis subscriber not found"));
            return;
        }

        if (subscriber) {
            subscriber->Stop();
            txdel subscriber;
        }

        if (callback)
            callback(RedisResult());
    }

    bool RedisSession::IsReady() const {
        // Never perform network I/O from IsReady(). Server modules call this
        // from their owner/update thread. The old synchronous PING waited on
        // the same lock as an in-flight command and could therefore freeze the
        // entire node when Redis or the network stopped responding.
        return !_IsClosed.load(std::memory_order_acquire)
            && _ConnectionHealthy.load(std::memory_order_acquire)
            && _SocketDescriptor.load(std::memory_order_acquire) >= 0;
    }

    void RedisSession::Close() {
        if (_IsClosed.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        _ConnectionHealthy.store(false, std::memory_order_release);

        // Interrupt a blocked hiredis read before waiting for the command
        // lock. FinishCommand() retains the session until every in-flight task
        // has observed the closed socket, so this cannot free the context out
        // from under the worker.
        const std::intptr_t descriptor =
            _SocketDescriptor.load(std::memory_order_acquire);
        if (descriptor >= 0) {
#if defined(_WIN32)
            ::shutdown(static_cast<SOCKET>(descriptor), SD_BOTH);
#else
            ::shutdown(static_cast<int>(descriptor), SHUT_RDWR);
#endif
        }

        bool release = false;
        {
            AUTO_LOCK(_Lock);
            release = _CurrentOptCount == 0;
        }

        if (release)
            ReleaseClosedSession();
    }
}
