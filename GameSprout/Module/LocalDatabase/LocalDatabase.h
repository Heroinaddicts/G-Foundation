#ifndef __LocalDatabase_h__
#define __LocalDatabase_h__

#include "Header.h"

class LocalDatabase : public ILocalDatabase {
public:
    virtual ~LocalDatabase() {}

    ILDBSession * CreateLDB(const std::string & path) override;
    ILDBSession * OpenLDB(const std::string & path) override;

private:
    void BeforeInitialize(GameSproutApi * const api) override;
    bool Initialize(GameSproutApi * const api) override;
    bool Launch(GameSproutApi * const api) override;
    void LaunchFinished(GameSproutApi * const api) override;
    void Update(GameSproutApi * const api) override;
    void Release(GameSproutApi * const api) override;
};

#endif //__LocalDatabase_h__
