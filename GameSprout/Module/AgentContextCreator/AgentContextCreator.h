#ifndef __AgentContextCreator_h__
#define __AgentContextCreator_h__

#include "Header.h"

class AgentContextCreator : public IAgentContextCreator {
public:
    virtual ~AgentContextCreator() {}

private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi * const api) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi * const api) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi * const api) override;
    void LaunchFinished(GameSproutEditor::GameSproutModuleApi * const api) override;
    void Update(GameSproutEditor::GameSproutModuleApi * const api) override;
    void Release(GameSproutEditor::GameSproutModuleApi * const api) override;
};

#endif //__AgentContextCreator_h__
