#ifndef __MysqlHeader_h__
#define __MysqlHeader_h__

#include "IMysql.h"
#include <mysql.h>
#include <errmsg.h>

#include "ITaskApi.h"

#include "SafeQueue.hpp"
#include "SafeSystem.hpp"
#include "SafeThread.hpp"
#include "XPool.hpp"
#include "XLock.hpp"

#include <list>
#include <atomic>
#include <set>

namespace Foundation {
    class MysqlThreadGuard final {
    public:
        MysqlThreadGuard() : _Ready(mysql_thread_init() == 0) {}
        ~MysqlThreadGuard() {
            if (_Ready)
                mysql_thread_end();
        }

        bool Ready() const {
            return _Ready;
        }

    private:
        bool _Ready;
    };

    class MysqlThreadEndGuard final {
    public:
        ~MysqlThreadEndGuard() {
            mysql_thread_end();
        }
    };

    class MysqlSession;
    struct QueryInfo {
        MysqlSession* _Session;
        std::string _Sql;
        fQueryCallback _Callback;
        fMysqlQueryCallback _CallbackEx;
    };

    struct ResultInfo {
        QueryInfo* _QueryInfo;
        int _Code;
        std::vector<std::vector<std::string>>* _Data;
        MysqlResult* _Result;
        ResultInfo() : _QueryInfo(nullptr), _Code(-1), _Data(nullptr), _Result(nullptr) {}
    };
}

#endif //__MysqlHeader_h__
