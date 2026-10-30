#ifndef __LDBSession_h__
#define __LDBSession_h__

#include "ILocalDatabase.h"
#include <lmdb.h>
#include <filesystem>
#include <unordered_map>

class LDBTable;

class LDBSession final : public ILDBSession {
public:
    ~LDBSession() override;

    static LDBSession * Create(const std::string & path);
    static LDBSession * Open(const std::string & path);

    bool CreateTable(const std::string & name) override;
    ILDBTable * GetTable(const std::string & name) override;
    bool WriteBatch(const std::vector<LDBWriteOperation> & operations) override;
    void Close() override;

    LDBSession(const LDBSession &) = delete;
    LDBSession & operator=(const LDBSession &) = delete;

private:
    friend class LDBTable;

    LDBSession(MDB_env * environment, const std::filesystem::path & path);
    static LDBSession * OpenInternal(const std::string & path, bool create);
    LDBTable * OpenTable(const std::string & name, bool create);
    bool IsValidKey(const std::string & key) const;

    MDB_env * _Environment;
    const std::filesystem::path _Path;
    const size_t _MaxKeySize;
    std::unordered_map<std::string, LDBTable *> _Tables;
};

#endif //__LDBSession_h__
