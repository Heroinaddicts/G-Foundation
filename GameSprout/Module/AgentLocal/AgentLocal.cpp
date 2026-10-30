#include "AgentLocal.h"

void AgentLocal::BeforeInitialize(GameSproutModuleApi * const api) {
    g_Self = this;
}

bool AgentLocal::Initialize(GameSproutModuleApi * const api) {
    g_GameSproutModuleApi = api;
    return true;
}

bool AgentLocal::Launch(GameSproutModuleApi * const api)  {
    return true;
}

void AgentLocal::LaunchFinished(GameSproutModuleApi * const api)  {

}

void AgentLocal::Update(GameSproutModuleApi * const api)  {

}

void AgentLocal::Release(GameSproutModuleApi * const api)  {

}
