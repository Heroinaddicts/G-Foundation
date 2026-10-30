#ifndef __LDBTable_h__
#define __LDBTable_h__

#include "ILocalDatabase.h"
#include <lmdb.h>

class LDBSession;

class LDBTable final : public ILDBTable {
public:
    ~LDBTable() override = default;

    bool IsExists(const std::string & key) const override;
    bool Set(const std::string & key, void * const data, const int len) override;
    bool Get(const std::string & key, std::vector<uint8_t> & data) const override;
    bool Delete(const std::string & key) override;
    bool ForEach(const fVisitCallback & callback) const override;

    LDBTable(const LDBTable &) = delete;
    LDBTable & operator=(const LDBTable &) = delete;

private:
    friend class LDBSession;
    LDBTable(LDBSession * const owner, MDB_dbi database) : _Owner(owner), _Database(database) {}

    bool Read(const std::string & key, std::vector<uint8_t> * data) const;

    LDBSession * const _Owner;
    const MDB_dbi _Database;
};

#endif //__LDBTable_h__
