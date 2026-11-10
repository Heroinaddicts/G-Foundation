#include "AgentContextCreator.h"

void AgentContextCreator::BeforeInitialize(GameSproutEditor::GameSproutModuleApi * const api) {
    g_Self = this;
}

bool AgentContextCreator::Initialize(GameSproutEditor::GameSproutModuleApi * const api) {
    g_GameSproutApi = api;
    return true;
}

bool AgentContextCreator::Launch(GameSproutEditor::GameSproutModuleApi * const api)  {
    return true;
}

void AgentContextCreator::LaunchFinished(GameSproutEditor::GameSproutModuleApi * const api)  {

}

void AgentContextCreator::Update(GameSproutEditor::GameSproutModuleApi * const api)  {

}

void AgentContextCreator::Release(GameSproutEditor::GameSproutModuleApi * const api)  {

}
