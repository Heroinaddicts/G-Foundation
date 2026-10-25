#pragma once

#include "ILocalDatabase.h"
#include <lmdb.h>

class LDBSession;

class LDBTable final : public ILDBTable {
public:
    ~LDBTable() override = default;

    bool IsExists(const std::string & key) const override;
    bool Set(const std::string & key, void * const data, const int len) override;
    bool Get(const std::string & key, std::vector<uint8_t> & data) const override;

    LDBTable(const LDBTable &) = delete;
    LDBTable & operator=(const LDBTable &) = delete;

private:
    friend class LDBSession;
    LDBTable(LDBSession & owner, MDB_dbi database) : _Owner(owner), _Database(database) {}

    bool Read(const std::string & key, std::vector<uint8_t> * data) const;

    LDBSession & _Owner;
    const MDB_dbi _Database;
};
