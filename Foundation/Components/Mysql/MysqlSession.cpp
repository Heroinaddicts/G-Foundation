#include "MysqlSession.h"

#include <new>

namespace Foundation {
    static Int64 ConvertAffectedRows(const my_ulonglong rows) {
        return rows == (my_ulonglong)-1 ? -1 : (Int64)rows;
    }

    static std::string MysqlString(const char* const text) {
        return text ? text : "";
    }

    static bool IsConnectionFailureCode(const int code) {
        // MySQL/MariaDB client transport failures. These errors invalidate the
        // transaction even if a later mysql_ping() could reconnect the handle.
        return code == 2006 || code == 2013 || code == 2055;
    }

    static void SetMysqlError(MYSQL* con, MysqlResult* result, const int code = 0, const char* fallback = nullptr) {
        if (!result)
            return;

        result->_Code = code != 0 ? code : (con ? (int)mysql_errno(con) : s_MysqlCodeClosed);
        if (con && mysql_error(con) && mysql_error(con)[0] != '\0')
            result->_Error = mysql_error(con);
        else if (fallback)
            result->_Error = fallback;
    }

    static MysqlColumnInfo ConvertField(const MYSQL_FIELD& field) {
        MysqlColumnInfo info;
        info._Name = MysqlString(field.name);
        info._OrgName = MysqlString(field.org_name);
        info._Table = MysqlString(field.table);
        info._OrgTable = MysqlString(field.org_table);
        info._Db = MysqlString(field.db);
        info._Type = (UInt32)field.type;
        info._Length = (UInt64)field.length;
        info._Flags = (UInt32)field.flags;
        info._Decimals = (UInt32)field.decimals;
        info._IsNumber = IS_NUM_FIELD(&field) != 0;
        return info;
    }

    MysqlSession* MysqlSession::Create(FoundationApi* foundation, MYSQL* con) {
        if (!foundation || !con) {
            if (con) {
                MysqlThreadGuard threadGuard;
                mysql_close(con);
            }
            return nullptr;
        }

        MysqlSession* session = new (std::nothrow) MysqlSession(foundation, con);
        if (!session) {
            MysqlThreadGuard threadGuard;
            mysql_close(con);
            return nullptr;
        }
        if (!session->Start()) {
            session->Close();
            delete session;
            return nullptr;
        }
        return session;
    }

    void MysqlSession::Release(MysqlSession* session) {
        if (session) {
            session->Close();
            delete session;
        }
    }

    void MysqlSession::Query(const std::string& sql, const fQueryCallback& fun) {
        PushQuery(sql, fun, nullptr);
    }

    void MysqlSession::QueryEx(const std::string& sql, const fMysqlQueryCallback& fun) {
        PushQuery(sql, nullptr, fun);
    }

    void MysqlSession::Execute(const std::string& sql, const fMysqlQueryCallback& fun) {
        PushQuery(sql, nullptr, fun);
    }

    void MysqlSession::Begin(const fMysqlQueryCallback& fun) {
        Execute("BEGIN", fun);
    }

    void MysqlSession::Commit(const fMysqlQueryCallback& fun) {
        Execute("COMMIT", fun);
    }

    void MysqlSession::Rollback(const fMysqlQueryCallback& fun) {
        Execute("ROLLBACK", fun);
    }

    std::string MysqlSession::EscapeString(const std::string& input) const {
        MysqlThreadGuard threadGuard;
        if (!threadGuard.Ready())
            return {};

        AUTO_LOCK(_ConnectionLock);
        if (!_Connection || _Closed.load(std::memory_order_acquire)) {
            return {};
        }

        std::string output(input.size() * 2 + 1, '\0');
        const unsigned long outputSize = mysql_real_escape_string(
            _Connection,
            output.data(),
            input.data(),
            static_cast<unsigned long>(input.size()));
        output.resize(outputSize);
        return output;
    }

    bool MysqlSession::IsReady() const {
        // IsReady() is polled from server owner threads. A synchronous
        // mysql_ping() can consume the complete socket timeout and stall all
        // modules in that process. Transport failures are made sticky by the
        // worker before dispatching query results; node-level supervisors use
        // asynchronous SELECT 1 probes for idle connections.
        return !_Closed.load(std::memory_order_acquire)
            && _ConnectionHealthy.load(std::memory_order_acquire);
    }

    void MysqlSession::PushQuery(const std::string& sql, const fQueryCallback& callback, const fMysqlQueryCallback& callbackEx) {
        QueryInfo* info = txnew QueryInfo();
        info->_Session = this;
        info->_Sql = sql;
        info->_Callback = callback;
        info->_CallbackEx = callbackEx;

        bool accepted = false;
        {
            AUTO_LOCK(_QueryQueueLock);
            if (!_Closed.load(std::memory_order_acquire)) {
                _QueryQueue.Push(info);
                accepted = true;
            }
        }
        if (accepted)
            return;

        try {
            if (callback)
                callback(s_MysqlCodeClosed, nullptr);

            if (callbackEx) {
                MysqlResult result;
                result._Code = s_MysqlCodeClosed;
                result._Error = "mysql session closed";
                callbackEx(result);
            }
        }
        catch (...) {
        }
        txdel info;
    }

    MysqlSession::MysqlSession(FoundationApi* foundation, MYSQL* con)
        : _Foundation(foundation), _Connection(con) {
    }

    MysqlSession::~MysqlSession() {
        Close();
    }

    void MysqlSession::Update(const bool all) {
        ResultInfo* ret;
        if (!_Foundation) {
            return;
        }
        const UInt64 tick = FoundationUtils::SafeSystem::Time::GetMicroSecond();
        while (_ResultQueue.Pull(ret)) {
            bool legacyResultDelivered = false;
            try {
                if (ret->_QueryInfo->_Callback) {
                    ret->_QueryInfo->_Callback(ret->_Code, ret->_Data);
                    legacyResultDelivered = true;
                }

                if (ret->_QueryInfo->_CallbackEx && ret->_Result)
                    ret->_QueryInfo->_CallbackEx(*ret->_Result);
            }
            catch (...) {
            }

            if (!legacyResultDelivered && ret->_Data)
                txdel ret->_Data;

            if (ret->_Result)
                txdel ret->_Result;

            txdel ret->_QueryInfo;
            txdel ret;
            if (FoundationUtils::SafeSystem::Time::GetMicroSecond() - tick > 1000 && !all) {
                break;
            }
        }
    }

    void MysqlSession::Run(const int, void* const) {
        MysqlThreadGuard threadGuard;
        if (!threadGuard.Ready()) {
            _Closed.store(true, std::memory_order_release);
            return;
        }
        while (!IsStopRequested()) {
            Deal();
        }
    }

    void MysqlSession::Close() {
        if (_Closed.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        _ConnectionHealthy.store(false, std::memory_order_release);

        MysqlThreadGuard threadGuard;
        RequestStop();
        Wait();
        Deal();
        Update(true);
        {
            AUTO_LOCK(_ConnectionLock);
            if (_Connection) {
                mysql_close(_Connection);
                _Connection = nullptr;
            }
        }
    }

    void MysqlSession::FillResultInfo(QueryInfo* query, ResultInfo* ret) {
        ret->_Result = txnew MysqlResult();
        if (query->_Callback)
            ret->_Data = txnew std::vector<std::vector<std::string>>();

        if (query->_Sql.empty()) {
            ret->_Result->_Code = s_MysqlCodeInvalidArgument;
            ret->_Result->_Error = "mysql sql is empty";
            ret->_Code = ret->_Result->_Code;
            return;
        }

        if (_Closed.load(std::memory_order_acquire)) {
            ret->_Result->_Code = s_MysqlCodeClosed;
            ret->_Result->_Error = "mysql session closed";
            ret->_Code = ret->_Result->_Code;
            return;
        }

        AUTO_LOCK(_ConnectionLock);
        if (_Closed.load(std::memory_order_acquire) || !_Connection) {
            ret->_Result->_Code = s_MysqlCodeClosed;
            ret->_Result->_Error = "mysql session closed";
            ret->_Code = ret->_Result->_Code;
            return;
        }

        if (mysql_real_query(
            _Connection,
            query->_Sql.data(),
            static_cast<unsigned long>(query->_Sql.size())) != 0) {
            SetMysqlError(_Connection, ret->_Result);
            if (IsConnectionFailureCode(ret->_Result->_Code)) {
                _ConnectionHealthy.store(false, std::memory_order_release);
            }
            ret->_Code = ret->_Result->_Code;
            return;
        }

        ret->_Result->_Code = s_MysqlCodeSuccess;
        ret->_Result->_Error.clear();

        while (true) {
            MysqlResultSet resultSet;
            resultSet._AffectedRows = ConvertAffectedRows(mysql_affected_rows(query->_Session->_Connection));
            resultSet._LastInsertId = (UInt64)mysql_insert_id(query->_Session->_Connection);
            ret->_Result->_AffectedRows = resultSet._AffectedRows;
            ret->_Result->_LastInsertId = resultSet._LastInsertId;

            MYSQL_RES* result = mysql_store_result(query->_Session->_Connection);
            if (result) {
                unsigned int fieldCount = mysql_num_fields(result);
                MYSQL_FIELD* fields = mysql_fetch_fields(result);
                resultSet._Columns.reserve(fieldCount);
                for (unsigned int i = 0; i < fieldCount; ++i) {
                    resultSet._Columns.push_back(ConvertField(fields[i]));
                }

                MYSQL_ROW row;
                while ((row = mysql_fetch_row(result)) != nullptr) {
                    unsigned long* lengths = mysql_fetch_lengths(result);
                    std::vector<MysqlCell> rowData;
                    rowData.reserve(fieldCount);
                    std::vector<std::string> legacyRow;
                    if (ret->_Data)
                        legacyRow.reserve(fieldCount);

                    for (unsigned int i = 0; i < fieldCount; ++i) {
                        MysqlCell cell;
                        cell._IsNull = row[i] == nullptr;
                        if (!cell._IsNull)
                            cell._Value.assign(row[i], lengths ? lengths[i] : strlen(row[i]));

                        if (ret->_Data)
                            legacyRow.push_back(cell._IsNull ? "nullptr" : cell._Value);

                        rowData.push_back(cell);
                    }

                    if (ret->_Data)
                        ret->_Data->push_back(legacyRow);

                    resultSet._Rows.push_back(rowData);
                }

                const int fetchError = static_cast<int>(mysql_errno(query->_Session->_Connection));
                if (fetchError != 0) {
                    mysql_free_result(result);
                    SetMysqlError(query->_Session->_Connection, ret->_Result, fetchError);
                    if (IsConnectionFailureCode(ret->_Result->_Code)) {
                        _ConnectionHealthy.store(false, std::memory_order_release);
                    }
                    ret->_Code = ret->_Result->_Code;
                    return;
                }

                mysql_free_result(result);
                ret->_Result->_ResultSets.push_back(resultSet);
            }
            else {
                if (mysql_field_count(query->_Session->_Connection) != 0) {
                    SetMysqlError(query->_Session->_Connection, ret->_Result);
                    if (IsConnectionFailureCode(ret->_Result->_Code)) {
                        _ConnectionHealthy.store(false, std::memory_order_release);
                    }
                    ret->_Code = ret->_Result->_Code;
                    return;
                }

                if (resultSet._AffectedRows >= 0 || resultSet._LastInsertId != 0)
                    ret->_Result->_ResultSets.push_back(resultSet);
            }

            int nextCode = mysql_next_result(query->_Session->_Connection);
            if (nextCode < 0)
                break;

            if (nextCode > 0) {
                SetMysqlError(query->_Session->_Connection, ret->_Result);
                if (IsConnectionFailureCode(ret->_Result->_Code)) {
                    _ConnectionHealthy.store(false, std::memory_order_release);
                }
                ret->_Code = ret->_Result->_Code;
                return;
            }
        }

        ret->_Code = ret->_Result->_Code;
    }

    void MysqlSession::Deal() {
        QueryInfo* query;
        bool needSleep = true;
        while (true) {
            {
                AUTO_LOCK(_QueryQueueLock);
                if (!_QueryQueue.Pull(query)) {
                    break;
                }
            }
            needSleep = false;
            ResultInfo* ret = txnew ResultInfo();
            ret->_QueryInfo = query;
            ret->_Code = s_MysqlCodeSuccess;
            _QueryExecuting.store(true, std::memory_order_release);
            FillResultInfo(query, ret);
            _QueryExecuting.store(false, std::memory_order_release);
            _ResultQueue.Push(ret);
        }

        if (needSleep)
            FoundationUtils::SafeSystem::Time::MillisecondSleep(2);
    }
}
