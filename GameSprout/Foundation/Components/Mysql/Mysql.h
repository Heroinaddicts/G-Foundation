#ifndef __Mysql_h__
#define __Mysql_h__

#include "Header.h"

#include <list>
#include <set>

namespace Foundation {
    class Mysql : public IMysql {
    public:
        virtual ~Mysql() {}

        void ConnectMysql(const MysqlConnectOptions& options, fConnectMysqlCallback callback) override;
        void ConnectMysqlEx(const MysqlConnectOptions& options, fConnectMysqlResultCallback callback) override;
        virtual void FreeQueryResult(const std::vector<std::vector<std::string>>* const result) override;

        bool Initialize(FoundationApi* const foundation) override;
        bool Launch(FoundationApi* const foundation) override;
        void Release(FoundationApi* const foundation) override;
        void EarlyUpdate(FoundationApi* const foundation) override;
        void Update(FoundationApi* const foundation) override;
        void LaterUpdate(FoundationApi* const foundation) override;

    private:
        FoundationApi* _Foundation = nullptr;
        std::set<MysqlSession*> _Sessions;
        std::list<MysqlSession*> _RecycleBin;
        std::atomic_bool _Releasing = false;
        bool _Launched = false;
    };

}
#endif //__Mysql_h__
