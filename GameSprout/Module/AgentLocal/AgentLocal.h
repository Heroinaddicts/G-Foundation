#ifndef __AgentLocal_h__
#define __AgentLocal_h__

#include "Header.h"

class AgentLocal : public IAgentLocal {
public:
    virtual ~AgentLocal() {}

private:
    void BeforeInitialize(GameSproutModuleApi * const api) override;
    bool Initialize(GameSproutModuleApi * const api) override;
    bool Launch(GameSproutModuleApi * const api) override;
    void LaunchFinished(GameSproutModuleApi * const api) override;
    void Update(GameSproutModuleApi * const api) override;
    void Release(GameSproutModuleApi * const api) override;
};

#endif //__AgentLocal_h__
