#include "AgentLocal.h"

void AgentLocal::BeforeInitialize(GameSproutApi * const api) {
    g_Self = this;
}

bool AgentLocal::Initialize(GameSproutApi * const api) {
    g_GameSproutApi = api;
    return true;
}

bool AgentLocal::Launch(GameSproutApi * const api)  {
    return true;
}

void AgentLocal::LaunchFinished(GameSproutApi * const api)  {

}

void AgentLocal::Update(GameSproutApi * const api)  {

}

void AgentLocal::Release(GameSproutApi * const api)  {

}
