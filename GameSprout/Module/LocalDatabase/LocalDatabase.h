#ifndef __LocalDatabase_h__
#define __LocalDatabase_h__

#include "Header.h"

class LocalDatabase : public ILocalDatabase {
public:
    virtual ~LocalDatabase() {}

    ILDBSession * CreateLDB(const std::string & path) override;
    ILDBSession * OpenLDB(const std::string & path) override;

private:
    void BeforeInitialize(GameSproutModuleApi * const api) override;
    bool Initialize(GameSproutModuleApi * const api) override;
    bool Launch(GameSproutModuleApi * const api) override;
    void LaunchFinished(GameSproutModuleApi * const api) override;
    void Update(GameSproutModuleApi * const api) override;
    void Release(GameSproutModuleApi * const api) override;
};

#endif //__LocalDatabase_h__
