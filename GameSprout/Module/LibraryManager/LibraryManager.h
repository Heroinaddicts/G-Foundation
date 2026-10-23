#ifndef __LibraryManager_h__
#define __LibraryManager_h__

#include "Header.h"

class LibraryManager : public ILibraryManager {
public:
    virtual ~LibraryManager() {}

private:
    void BeforeInitialize(GameSproutApi * const api) override;
    bool Initialize(GameSproutApi * const api) override;
    bool Launch(GameSproutApi * const api) override;
    void LaunchFinished(GameSproutApi * const api) override;
    void Update(GameSproutApi * const api) override;
    void Release(GameSproutApi * const api) override;
};

#endif //__LibraryManager_h__
