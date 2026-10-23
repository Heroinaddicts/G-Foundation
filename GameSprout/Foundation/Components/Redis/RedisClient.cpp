#include "RedisClient.h"
#include "RedisSession.h"

namespace Foundation {
    static timeval RedisTimeout(const UInt32 milliseconds) {
        timeval timeout{};
        timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(milliseconds / 1000);
        timeout.tv_usec = static_cast<decltype(timeout.tv_usec)>((milliseconds % 1000) * 1000);
        return timeout;
    }

    static Int64 RedisCodeToInt(const eRedisCode code) {
        return static_cast<Int64>(code);
    }

    struct RedisConnectInfo {
        ~RedisConnectInfo() {
            if (_Con) {
                redisFree(_Con);
                _Con = nullptr;
            }
        }

        redisContext* _Con = nullptr;
        RedisResult _Result;
    };

    bool RedisClient::Initialize(FoundationApi* const foundation) {
        _Foundation = foundation;
        _Releasing.store(false, std::memory_order_release);
        _Launched = false;
        return _Foundation && _Foundation->TaskApi();
    }

    bool RedisClient::Launch(FoundationApi* const foundation) {
        if (foundation != _Foundation || _Launched) {
            return false;
        }
        _Launched = true;
        return true;
    }

    void RedisClient::Release(FoundationApi* const foundation) {
        _Releasing.store(true, std::memory_order_release);
        _Launched = false;
        std::vector<RedisSession*> sessions;
        {
            AUTO_LOCK(_SessionsLock);
            sessions.assign(_Sessions.begin(), _Sessions.end());
        }
        for (RedisSession* session : sessions) {
            if (session) {
                session->Close();
            }
        }
    }

    void RedisClient::EarlyUpdate(FoundationApi* const foundation) {
    }

    void RedisClient::Update(FoundationApi* const foundation) {
    }

    void RedisClient::LaterUpdate(FoundationApi* const foundation) {
    }

    void RedisClient::ConnectRedis(const RedisConnectOptions& options, fConnectRedisCallback callback) {
        ConnectRedisEx(options,
            [=](IRedisSession* const session, const RedisResult&) {
                if (callback)
                    callback(session);
            }
        );
    }

    void RedisClient::ConnectRedisEx(const RedisConnectOptions& options, fConnectRedisResultCallback callback) {
        if (options._Host.empty() || options._Port == 0
            || options._ConnectTimeoutMilliseconds == 0
            || options._OperationTimeoutMilliseconds == 0) {
            if (callback) {
                RedisResult result;
                result._Code = eRedisCode::InvalidArgument;
                result._Error = "redis connection options are invalid";
                callback(nullptr, result);
            }
            return;
        }
        if (!_Launched || _Releasing.load(std::memory_order_acquire) || !_Foundation || !_Foundation->TaskApi()) {
            if (callback) {
                RedisResult result;
                result._Code = eRedisCode::Closed;
                result._Error = "redis component is not initialized";
                callback(nullptr, result);
            }
            return;
        }
        TaskSubmitOptions taskOptions;
        taskOptions._OwnerId = FoundationTaskOwnerId;
        taskOptions._ExecutionType = eTaskExecutionType::Concurrent;
        const TaskSubmitResult submitResult = _Foundation->TaskApi()->SubmitTask(taskOptions,
            [=](OUT Int64& code, ITaskContext& context) {
                RedisConnectInfo* info = txnew RedisConnectInfo();
                context.SetOwned(info);
                const timeval connectTimeout = RedisTimeout(options._ConnectTimeoutMilliseconds);
                redisContext* con = redisConnectWithTimeout(options._Host.c_str(), options._Port, connectTimeout);
                if (nullptr == con || con->err) {
                    if (con) {
                        info->_Result._Code = eRedisCode::CommandError;
                        info->_Result._Error = con->errstr;
                        redisFree(con);
                    }
                    else {
                        info->_Result._Code = eRedisCode::CommandError;
                        info->_Result._Error = "can't allocate redis context";
                    }

                    code = RedisCodeToInt(info->_Result._Code);
                    return false;
                }

                const timeval operationTimeout = RedisTimeout(options._OperationTimeoutMilliseconds);
                if (redisSetTimeout(con, operationTimeout) != REDIS_OK) {
                    info->_Result._Code = eRedisCode::CommandError;
                    info->_Result._Error = con->errstr[0] != '\0'
                        ? con->errstr
                        : "can't configure redis operation timeout";
                    redisFree(con);
                    code = RedisCodeToInt(info->_Result._Code);
                    return false;
                }

                if (!options._Password.empty()) {
                    redisReply* reply = (redisReply*)redisCommand(con, "AUTH %s", options._Password.c_str());
                    if (nullptr == reply) {
                        info->_Result._Code = eRedisCode::CommandError;
                        info->_Result._Error = con->errstr;
                        redisFree(con);
                        code = RedisCodeToInt(info->_Result._Code);
                        return false;
                    }
                    if (reply->type == REDIS_REPLY_ERROR) {
                        info->_Result._Code = eRedisCode::ReplyError;
                        info->_Result._Error.assign(reply->str, reply->len);
                        freeReplyObject(reply);
                        redisFree(con);
                        code = RedisCodeToInt(info->_Result._Code);
                        return false;
                    }
                    freeReplyObject(reply);
                }

                info->_Con = con;
                info->_Result._Code = eRedisCode::Success;
                code = RedisCodeToInt(eRedisCode::Success);
                return true;
            },
            [=](const TaskCompletion&, const bool succeed, const ITaskContext& context) {
                RedisConnectInfo* info = context.Get<RedisConnectInfo>();
                if (!info) {
                    if (callback) {
                        RedisResult result;
                        result._Code = eRedisCode::Closed;
                        result._Error = context.IsCancellationRequested()
                            ? "redis connect task was cancelled"
                            : "redis connect task failed";
                        callback(nullptr, result);
                    }
                    return;
                }
                if (_Releasing.load(std::memory_order_acquire)) {
                    info->_Result._Code = eRedisCode::Closed;
                    info->_Result._Error = "redis component is releasing";
                    if (callback) {
                        callback(nullptr, info->_Result);
                    }
                }
                else if (succeed && info->_Result.Success() && info->_Con) {
                    if (callback) {
                        RedisSession* session = txnew RedisSession(this, _Foundation, options, info->_Con);
                        info->_Con = nullptr;
                        RegisterSession(session);
                        try {
                            callback(session, info->_Result);
                        }
                        catch (...) {
                            session->Close();
                            throw;
                        }
                    }
                }
                else {
                    if (callback)
                        callback(nullptr, info->_Result);
                }
            }
        );
        if (!submitResult.Accepted() && callback) {
            RedisResult result;
            result._Code = eRedisCode::Closed;
            result._Error = "redis task system is unavailable";
            callback(nullptr, result);
        }
    }

    void RedisClient::RegisterSession(RedisSession* session) {
        if (!session) {
            return;
        }
        AUTO_LOCK(_SessionsLock);
        _Sessions.insert(session);
    }

    void RedisClient::UnregisterSession(RedisSession* session) {
        AUTO_LOCK(_SessionsLock);
        _Sessions.erase(session);
    }

    Int64 RedisClient::NextSessionIndex() {
        return _SessionIndexOffset.fetch_add(1, std::memory_order_relaxed);
    }
}
