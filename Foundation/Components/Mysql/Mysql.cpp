#include "Mysql.h"
#include "MysqlSession.h"
#include "Foundation.h"

namespace Foundation {
    static unsigned int MysqlTimeoutSeconds(const UInt32 milliseconds) {
        return static_cast<unsigned int>(
            milliseconds / 1000 + (milliseconds % 1000 == 0 ? 0 : 1));
    }

    static bool ConfigureConnection(MYSQL* const connection, const MysqlConnectOptions& options, MysqlResult& result) {
        unsigned int connectTimeout = MysqlTimeoutSeconds(options._ConnectTimeoutMilliseconds);
        unsigned int readTimeout = MysqlTimeoutSeconds(options._ReadTimeoutMilliseconds);
        unsigned int writeTimeout = MysqlTimeoutSeconds(options._WriteTimeoutMilliseconds);
        if (mysql_options(connection, MYSQL_OPT_CONNECT_TIMEOUT, &connectTimeout) != 0
            || mysql_options(connection, MYSQL_OPT_READ_TIMEOUT, &readTimeout) != 0
            || mysql_options(connection, MYSQL_OPT_WRITE_TIMEOUT, &writeTimeout) != 0) {
            result._Code = s_MysqlCodeInvalidArgument;
            result._Error = mysql_error(connection);
            if (result._Error.empty())
                result._Error = "can't configure mysql connection timeout";
            return false;
        }

        my_bool enforceTls = options._TlsMode != eMysqlTlsMode::Preferred;
        my_bool verifyServer = options._TlsMode == eMysqlTlsMode::VerifyServerCertificate;
        if (mysql_options(connection, MYSQL_OPT_SSL_ENFORCE, &enforceTls) != 0
            || mysql_options(connection, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &verifyServer) != 0
            || (!options._TlsCaFile.empty()
                && mysql_options(
                    connection,
                    MYSQL_OPT_SSL_CA,
                    options._TlsCaFile.c_str()) != 0)) {
            result._Code = s_MysqlCodeInvalidArgument;
            result._Error = mysql_error(connection);
            if (result._Error.empty())
                result._Error = "can't configure mysql TLS options";
            return false;
        }
        return true;
    }

    struct MysqlConnectInfo {
        ~MysqlConnectInfo() {
            if (_Con) {
                MysqlThreadGuard threadGuard;
                mysql_close(_Con);
                _Con = nullptr;
            }
        }

        MYSQL* _Con = nullptr;
        MysqlResult _Result;
    };

    bool Mysql::Initialize(FoundationApi* const foundation) {
        _Foundation = foundation;
        _Releasing.store(false, std::memory_order_release);
        _Launched = false;
        return _Foundation && _Foundation->TaskApi();
    }

    bool Mysql::Launch(FoundationApi* const foundation) {
        if (foundation != _Foundation || _Launched) {
            return false;
        }
        _Launched = true;
        return true;
    }

    void Mysql::Release(FoundationApi* const foundation) {
        _Releasing.store(true, std::memory_order_release);
        for (MysqlSession* session : _Sessions) {
            if (session && !session->Closed()) {
                session->Close();
            }
            MysqlSession::Release(session);
        }
        _Sessions.clear();
        _RecycleBin.clear();
        _Launched = false;
        _Foundation = nullptr;
    }

    void Mysql::EarlyUpdate(FoundationApi* const foundation) {
    }

    void Mysql::Update(FoundationApi* const foundation) {
    }

    void Mysql::LaterUpdate(FoundationApi* const foundation) {
        for (auto itor = _Sessions.begin(); itor != _Sessions.end(); itor++) {
            if (!(*itor)->Closed())
                (*itor)->Update();
            else
                _RecycleBin.push_back(*itor);
        }

        for (auto itor = _RecycleBin.begin(); itor != _RecycleBin.end(); itor++) {
            MysqlSession::Release(*itor);
            _Sessions.erase(*itor);
        }

        _RecycleBin.clear();
    }

    void Mysql::ConnectMysql(const MysqlConnectOptions& options, fConnectMysqlCallback callback) {
        ConnectMysqlEx(options,
            [=](IMysqlSession* session, const MysqlResult&) {
                if (callback)
                    callback(session);
            }
        );
    }

    void Mysql::ConnectMysqlEx(const MysqlConnectOptions& options, fConnectMysqlResultCallback callback) {
        if (options._Port == 0
            || options._ConnectTimeoutMilliseconds == 0
            || options._ReadTimeoutMilliseconds == 0
            || options._WriteTimeoutMilliseconds == 0) {
            if (callback) {
                MysqlResult result;
                result._Code = s_MysqlCodeInvalidArgument;
                result._Error = "mysql connection options are invalid";
                callback(nullptr, result);
            }
            return;
        }
        if (!_Launched || _Releasing.load(std::memory_order_acquire) || !_Foundation || !_Foundation->TaskApi()) {
            if (callback) {
                MysqlResult result;
                result._Code = s_MysqlCodeClosed;
                result._Error = "mysql component is not initialized";
                callback(nullptr, result);
            }
            return;
        }
        TaskSubmitOptions taskOptions;
        taskOptions._OwnerId = FoundationTaskOwnerId;
        taskOptions._ExecutionType = eTaskExecutionType::Concurrent;
        const TaskSubmitResult submitResult = _Foundation->TaskApi()->SubmitTask(taskOptions,
            [=](OUT Int64& code, ITaskContext& context) {
                MysqlConnectInfo* info = txnew MysqlConnectInfo();
                context.SetOwned(info);
                MYSQL* con = mysql_init(nullptr);
                MysqlThreadEndGuard threadEndGuard;
                if (nullptr == con) {
                    info->_Result._Code = s_MysqlCodeClosed;
                    info->_Result._Error = "mysql_init error";
                    code = info->_Result._Code;
                    return false;
                }

                if (!ConfigureConnection(con, options, info->_Result)) {
                    mysql_close(con);
                    code = info->_Result._Code;
                    return false;
                }

                const char* const database = options._Database.empty() ? nullptr : options._Database.c_str();
                const unsigned long clientFlags = options._AllowMultiStatements
                    ? CLIENT_MULTI_STATEMENTS
                    : 0;
                if (mysql_real_connect(
                    con,
                    options._Host.c_str(),
                    options._Username.c_str(),
                    options._Password.c_str(),
                    database,
                    options._Port,
                    nullptr,
                    clientFlags) == nullptr) {
                    info->_Result._Code = (int)mysql_errno(con);
                    info->_Result._Error = mysql_error(con);
                    mysql_close(con);
                    code = info->_Result._Code;
                    return false;
                }

                info->_Con = con;
                info->_Result._Code = s_MysqlCodeSuccess;
                code = s_MysqlCodeSuccess;
                return true;
            },
            [=](const TaskCompletion& completion, const bool succeed, const ITaskContext& context) {
                MysqlConnectInfo* info = context.Get<MysqlConnectInfo>();
                if (!info) {
                    if (callback) {
                        MysqlResult result;
                        result._Code = s_MysqlCodeClosed;
                        result._Error = context.IsCancellationRequested()
                            ? "mysql connect task was cancelled"
                            : "mysql connect task failed";
                        callback(nullptr, result);
                    }
                    return;
                }
                if (_Releasing.load(std::memory_order_acquire)) {
                    info->_Result._Code = s_MysqlCodeClosed;
                    info->_Result._Error = "mysql component is releasing";
                    if (callback) {
                        callback(nullptr, info->_Result);
                    }
                }
                else if (succeed && completion._Code == s_MysqlCodeSuccess && info->_Con) {
                    if (!callback) {
                        return;
                    }

                    MYSQL* const connection = info->_Con;
                    info->_Con = nullptr;
                    MysqlSession* session = MysqlSession::Create(_Foundation, connection);
                    if (nullptr == session) {
                        info->_Result._Code = s_MysqlCodeClosed;
                        info->_Result._Error = "create mysql session failed";
                        if (callback)
                            callback(nullptr, info->_Result);
                    }
                    else {
                        _Sessions.insert(session);
                        if (callback) {
                            try {
                                callback(session, info->_Result);
                            }
                            catch (...) {
                                _Sessions.erase(session);
                                MysqlSession::Release(session);
                                throw;
                            }
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
            MysqlResult result;
            result._Code = s_MysqlCodeClosed;
            result._Error = "mysql task system is unavailable";
            callback(nullptr, result);
        }
    }

    void Mysql::FreeQueryResult(const std::vector<std::vector<std::string>>* const result) {
        if (result) {
            txdel result;
        }
    }
}
