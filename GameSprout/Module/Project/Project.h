#ifndef __Project_h__
#define __Project_h__

#include "Header.h"

class Project : public IProject {
public:
    virtual ~Project() {}

    const std::string & GetProjectRoot() const override;
private:
    bool Initialize(GameSproutModuleApi * const api) override;
    bool Launch(GameSproutModuleApi * const api) override;
    void LaunchFinished(GameSproutModuleApi * const api) override;
    void Update(GameSproutModuleApi * const api) override;
    void Release(GameSproutModuleApi * const api) override;

private:
    std::string _Root;
};

#endif //__Project_h__
