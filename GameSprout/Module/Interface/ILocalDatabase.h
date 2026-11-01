#ifndef __ILocalDatabase_h__
#define __ILocalDatabase_h__

#include "GameSproutEditorApi.h"

#include <vector>
#include <cstdint>
#include <functional>
#include <string>

struct LDBWriteOperation {
    std::string Table;
    std::string Key;
    std::vector<uint8_t> Data;
    bool Delete = false;
};

// All database interfaces are synchronous and must be called on the main thread.
// Sessions/tables have no internal synchronization; TaskApi workers must not access them.
class ILDBTable {
public:
    virtual ~ILDBTable() {}
    using fVisitCallback = std::function<bool(const std::string& key, const void* const data, const int len)>;

    virtual bool IsExists(const std::string & key) const = 0;
    // Inserts or overwrites. A null buffer is valid only when len is zero.
    virtual bool Set(const std::string & key, void * const data, const int len) = 0;
    // Copies into caller-owned storage; failure clears data.
    virtual bool Get(const std::string & key, std::vector<uint8_t>& data) const = 0;
    // Missing keys succeed. A closed session or invalid key fails.
    virtual bool Delete(const std::string & key) = 0;
    // Synchronous: true continues, false stops successfully; an empty table succeeds.
    // Returns false for an empty callback, a closed session or a database read error.
    // key/data are borrowed for the current callback only. Do not access, close or
    // destroy the owning session/tables from the callback. Callback exceptions propagate.
    virtual bool ForEach(const fVisitCallback& callback) const = 0;
};

class ILDBSession {
public:
    virtual ~ILDBSession() {}

    // Creates if absent; succeeds when the table already exists.
    virtual bool CreateTable(const std::string & name) = 0;
    // Borrowed pointer owned by this session. Missing tables return nullptr.
    virtual ILDBTable * GetTable(const std::string & name) = 0;

    // All operations commit in one transaction or none do. Tables must exist.
    virtual bool WriteBatch(const std::vector<LDBWriteOperation> & operations) = 0;

    // Idempotent. Table wrappers remain valid but reject operations after close.
    virtual void Close() = 0;
};

class ILocalDatabase : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~ILocalDatabase() {}

    // Paths name LMDB directories. The caller deletes the returned session.
    // Creation rejects an existing database; opening never creates a missing one.
    virtual ILDBSession * CreateLDB(const std::string &path) = 0;
    virtual ILDBSession * OpenLDB(const std::string &path) = 0;
};

#endif //__ILocalDatabase_h__
