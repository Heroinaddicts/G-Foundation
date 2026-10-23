#ifndef __Project_h__
#define __Project_h__

#include "Header.h"

class Project : public IProject {
public:
    virtual ~Project() {}

    const std::string & GetProjectRoot() const override;
private:
    bool Initialize(GameSproutApi * const api) override;
    bool Launch(GameSproutApi * const api) override;
    void LaunchFinished(GameSproutApi * const api) override;
    void Update(GameSproutApi * const api) override;
    void Release(GameSproutApi * const api) override;

private:
    std::string _Root;
};

#endif //__Project_h__
