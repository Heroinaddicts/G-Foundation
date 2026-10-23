#ifndef MYSQL_API_H
#define MYSQL_API_H

#include "MultiSys.hpp"

namespace Foundation {
    class IMysqlSession;

    inline constexpr int s_MysqlCodeSuccess = 0;
    inline constexpr int s_MysqlCodeClosed = -1;
    inline constexpr int s_MysqlCodeInvalidArgument = -2;

    enum class eMysqlTlsMode {
        Preferred = 0,
        Required,
        VerifyServerCertificate
    };

    struct MysqlConnectOptions {
        std::string _Host;
        UInt16 _Port = 3306;
        std::string _Database;
        std::string _Username;
        std::string _Password;
        UInt32 _ConnectTimeoutMilliseconds = 10000;
        UInt32 _ReadTimeoutMilliseconds = 30000;
        UInt32 _WriteTimeoutMilliseconds = 30000;
        bool _AllowMultiStatements = false;
        eMysqlTlsMode _TlsMode = eMysqlTlsMode::Preferred;
        std::string _TlsCaFile;
    };

    struct MysqlColumnInfo {
        std::string _Name;
        std::string _OrgName;
        std::string _Table;
        std::string _OrgTable;
        std::string _Db;
        UInt32 _Type = 0;
        UInt64 _Length = 0;
        UInt32 _Flags = 0;
        UInt32 _Decimals = 0;
        bool _IsNumber = false;
    };

    struct MysqlCell {
        bool _IsNull = true;
        std::string _Value;

        __forceinline const char* Data() const {
            return _Value.data();
        }

        __forceinline size_t Size() const {
            return _Value.size();
        }
    };

    struct MysqlResultSet {
        std::vector<MysqlColumnInfo> _Columns;
        std::vector<std::vector<MysqlCell>> _Rows;
        Int64 _AffectedRows = -1;
        UInt64 _LastInsertId = 0;
    };

    struct MysqlResult {
        int _Code = s_MysqlCodeSuccess;
        std::string _Error;
        std::vector<MysqlResultSet> _ResultSets;
        Int64 _AffectedRows = -1;
        UInt64 _LastInsertId = 0;

        __forceinline bool Success() const {
            return _Code == s_MysqlCodeSuccess;
        }
    };

    using fQueryCallback = std::function<void(int code, const std::vector<std::vector<std::string>>* result)>;
    using fMysqlQueryCallback = std::function<void(const MysqlResult&)>;
    using fConnectMysqlCallback = std::function<void(IMysqlSession*)>;
    using fConnectMysqlResultCallback = std::function<void(IMysqlSession*, const MysqlResult&)>;

    class IMysqlSession {
    public:
        virtual ~IMysqlSession() {}

        virtual void Query(const std::string& sql, const fQueryCallback& fun) = 0;
        virtual void QueryEx(const std::string& sql, const fMysqlQueryCallback& fun) = 0;
        virtual void Execute(const std::string& sql, const fMysqlQueryCallback& fun) = 0;
        virtual void Begin(const fMysqlQueryCallback& fun) = 0;
        virtual void Commit(const fMysqlQueryCallback& fun) = 0;
        virtual void Rollback(const fMysqlQueryCallback& fun) = 0;
        virtual std::string EscapeString(const std::string& input) const = 0;
        virtual bool IsReady() const = 0;
        virtual void Close() = 0;
    };

    class IMysqlApi {
    public:
        virtual ~IMysqlApi() {}
        virtual void ConnectMysql(const std::string& host, const int port, const std::string& db, const std::string& username, const std::string& password, fConnectMysqlCallback callback) {
            if (port <= 0 || port > 65535) {
                if (callback)
                    callback(nullptr);
                return;
            }
            MysqlConnectOptions options;
            options._Host = host;
            options._Port = static_cast<UInt16>(port);
            options._Database = db;
            options._Username = username;
            options._Password = password;
            options._AllowMultiStatements = true;
            ConnectMysql(options, callback);
        }
        virtual void ConnectMysqlEx(const std::string& host, const int port, const std::string& db, const std::string& username, const std::string& password, fConnectMysqlResultCallback callback) {
            if (port <= 0 || port > 65535) {
                if (callback) {
                    MysqlResult result;
                    result._Code = s_MysqlCodeInvalidArgument;
                    result._Error = "mysql port is invalid";
                    callback(nullptr, result);
                }
                return;
            }
            MysqlConnectOptions options;
            options._Host = host;
            options._Port = static_cast<UInt16>(port);
            options._Database = db;
            options._Username = username;
            options._Password = password;
            options._AllowMultiStatements = true;
            ConnectMysqlEx(options, callback);
        }
        virtual void ConnectMysql(const MysqlConnectOptions& options, fConnectMysqlCallback callback) = 0;
        virtual void ConnectMysqlEx(const MysqlConnectOptions& options, fConnectMysqlResultCallback callback) = 0;
        virtual void FreeQueryResult(const std::vector<std::vector<std::string>>* const result) = 0;
    };
}

#endif // MYSQL_API_H
