#ifndef __ILibraryManager_h__
#define __ILibraryManager_h__

#include "GameSproutApi.h"
#include "GRHIApi.h"
#include "ILocalDatabase.h"

class ILibraryManager : public GameSprout::IGameSproutModule {
public:
    virtual ~ILibraryManager() {}

    virtual ILDBSession * GetLDBSession() const = 0;
};

#endif //__ILibraryManager_h__
