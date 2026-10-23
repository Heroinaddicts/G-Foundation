#include "GuidCreator.h"

void GuidCreator::BeforeInitialize(GameSproutApi * const api) {
    g_Self = this;
}

bool GuidCreator::Initialize(GameSproutApi * const api) {
    g_GameSproutApi = api;
    return true;
}

bool GuidCreator::Launch(GameSproutApi * const api)  {
    return true;
}

void GuidCreator::LaunchFinished(GameSproutApi * const api)  {

}

void GuidCreator::Update(GameSproutApi * const api)  {
    
}

void GuidCreator::Release(GameSproutApi * const api)  {
    
}
