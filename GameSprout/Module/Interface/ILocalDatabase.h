#ifndef __ILocalDatabase_h__
#define __ILocalDatabase_h__

#include "GameSproutApi.h"
#include "GRHIApi.h"

#include <vector>
#include <cstdint>
#include <string>

// Sessions and their tables are used on a single thread; no internal synchronization.
class ILDBTable {
public:
    virtual ~ILDBTable() {}

    virtual bool IsExists(const std::string & key) const = 0;
    // Inserts or overwrites. A null buffer is valid only when len is zero.
    virtual bool Set(const std::string & key, void * const data, const int len) = 0;
    // Copies into caller-owned storage; failure clears data.
    virtual bool Get(const std::string & key, std::vector<uint8_t>& data) const = 0;
};

class ILDBSession {
public:
    virtual ~ILDBSession() {}

    // Creates if absent; succeeds when the table already exists.
    virtual bool CreateTable(const std::string & name) = 0;
    // Borrowed pointer owned by this session. Missing tables return nullptr.
    virtual ILDBTable * GetTable(const std::string & name) = 0;

    // Idempotent. Table wrappers remain valid but reject operations after close.
    virtual void Close() = 0;
};

class ILocalDatabase : public GameSprout::IGameSproutModule {
public:
    virtual ~ILocalDatabase() {}

    // Paths name LMDB directories. The caller deletes the returned session.
    // Creation rejects an existing database; opening never creates a missing one.
    virtual ILDBSession * CreateLDB(const std::string &path) = 0;
    virtual ILDBSession * OpenLDB(const std::string &path) = 0;
};

#endif //__ILocalDatabase_h__
