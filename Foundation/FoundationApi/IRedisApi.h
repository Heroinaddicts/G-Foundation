#ifndef REDIS_API_H
#define REDIS_API_H

#include "MultiSys.hpp"

namespace Foundation {
    struct RedisConnectOptions {
        std::string _Host;
        UInt16 _Port = 6379;
        std::string _Password;
        UInt32 _ConnectTimeoutMilliseconds = 10000;
        UInt32 _OperationTimeoutMilliseconds = 30000;
    };

    enum class eRedisCode : int {
        Success = 0,
        Closed = -1,
        CommandError = -2,
        ReplyError = -3,
        InvalidArgument = -4
    };

    enum class eRedisValueType {
        None = 0,
        String,
        Array,
        Integer,
        Nil,
        Status,
        Error,
        Double,
        Bool,
        Map,
        Set
    };

    struct RedisValue {
        eRedisValueType _Type = eRedisValueType::None;
        std::string _String;
        Int64 _Integer = 0;
        double _Double = 0.0;
        bool _Boolean = false;
        std::vector<RedisValue> _Children;

        __forceinline bool IsNil() const {
            return _Type == eRedisValueType::Nil;
        }

        __forceinline const char* Data() const {
            return _String.data();
        }

        __forceinline size_t Size() const {
            return _String.size();
        }
    };

    struct RedisResult {
        eRedisCode _Code = eRedisCode::Success;
        std::string _Error;
        RedisValue _Value;

        __forceinline bool Success() const {
            return _Code == eRedisCode::Success;
        }
    };

    struct RedisMessage {
        std::string _Channel;
        std::string _Pattern;
        std::string _Data;

        __forceinline const char* Data() const {
            return _Data.data();
        }

        __forceinline size_t Size() const {
            return _Data.size();
        }
    };

    using fBoolResult = std::function<void(bool)>;
    using fStringResult = std::function<void(bool, const char*)>;
    using fIntResult = std::function<void(bool, int64_t)>;
    using fVectorResult = std::function<void(bool, const std::vector<std::string>&)>;
    using fRedisResult = std::function<void(const RedisResult&)>;
    using fRedisMessageCallback = std::function<void(const RedisMessage&)>;

    class IRedisSession;
    using fConnectRedisCallback = std::function<void(IRedisSession* const)>;
    using fConnectRedisResultCallback = std::function<void(IRedisSession* const, const RedisResult&)>;

    class IRedisSession {
    public:
        virtual ~IRedisSession() {}

        virtual void AsyncGet(const std::string& key, const fStringResult& callback) = 0;
        virtual void AsyncSet(const std::string& key, const std::string& value, const fBoolResult& callback) = 0;

        virtual void AsyncSetWhenNotExists(const std::string& key, const std::string& value, const fBoolResult& callback) = 0;
        virtual void AsyncCAS(const std::string& key, const std::string& compare, const std::string& value, const fBoolResult& callback) = 0;

        virtual void AsyncGetEx(const std::string& key, const fRedisResult& callback) = 0;
        virtual void AsyncDel(const std::string& key, const fRedisResult& callback) = 0;
        virtual void AsyncExists(const std::string& key, const fRedisResult& callback) = 0;
        virtual void AsyncExpire(const std::string& key, const Int64 seconds, const fRedisResult& callback) = 0;
        virtual void AsyncTTL(const std::string& key, const fRedisResult& callback) = 0;
        virtual void AsyncPersist(const std::string& key, const fRedisResult& callback) = 0;
        virtual void AsyncEval(const std::string& script, const std::vector<std::string>& keys, const std::vector<std::string>& args, const fRedisResult& callback) = 0;
        virtual void AsyncRawCommand(const std::vector<std::string>& command, const fRedisResult& callback) = 0;

        virtual void AsyncSubscribe(const std::string& channel, const fRedisMessageCallback& messageCallback, const fRedisResult& callback) = 0;
        virtual void AsyncUnsubscribe(const std::string& channel, const fRedisResult& callback) = 0;

        virtual bool IsReady() const = 0;
        virtual void Close() = 0;
    };

    class IRedisApi {
    public:
        virtual ~IRedisApi() {}
        virtual void ConnectRedis(const std::string& host, const int port, const std::string& password, fConnectRedisCallback callback) {
            if (port <= 0 || port > 65535) {
                if (callback)
                    callback(nullptr);
                return;
            }
            RedisConnectOptions options;
            options._Host = host;
            options._Port = static_cast<UInt16>(port);
            options._Password = password;
            ConnectRedis(options, callback);
        }
        virtual void ConnectRedisEx(const std::string& host, const int port, const std::string& password, fConnectRedisResultCallback callback) {
            if (port <= 0 || port > 65535) {
                if (callback) {
                    RedisResult result;
                    result._Code = eRedisCode::InvalidArgument;
                    result._Error = "redis port is invalid";
                    callback(nullptr, result);
                }
                return;
            }
            RedisConnectOptions options;
            options._Host = host;
            options._Port = static_cast<UInt16>(port);
            options._Password = password;
            ConnectRedisEx(options, callback);
        }
        virtual void ConnectRedis(const RedisConnectOptions& options, fConnectRedisCallback callback) = 0;
        virtual void ConnectRedisEx(const RedisConnectOptions& options, fConnectRedisResultCallback callback) = 0;
    };
}

#endif // REDIS_API_H
