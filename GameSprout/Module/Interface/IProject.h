#ifndef __IProject_h__
#define __IProject_h__

#include "GameSproutApi.h"
#include "GRHIApi.h"

class IProject : public GameSprout::IGameSproutModule {
public:
    virtual ~IProject() {}

    virtual const std::string & GetProjectRoot() const = 0;
};

#endif //__IProject_h__
