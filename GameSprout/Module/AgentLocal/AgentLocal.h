#ifndef __AgentLocal_h__
#define __AgentLocal_h__

#include "Header.h"

class AgentLocal : public IAgentLocal {
public:
    virtual ~AgentLocal() {}

private:
    void BeforeInitialize(GameSproutApi * const api) override;
    bool Initialize(GameSproutApi * const api) override;
    bool Launch(GameSproutApi * const api) override;
    void LaunchFinished(GameSproutApi * const api) override;
    void Update(GameSproutApi * const api) override;
    void Release(GameSproutApi * const api) override;
};

#endif //__AgentLocal_h__
