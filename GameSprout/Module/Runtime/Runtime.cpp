#include "Runtime.h"

void Runtime::BeforeInitialize(GameSproutApi * const api) {
    g_Self = this;
}

bool Runtime::Initialize(GameSproutApi * const api) {
    g_GameSproutApi = api;
    return true;
}

bool Runtime::Launch(GameSproutApi * const api)  {
    return true;
}

void Runtime::LaunchFinished(GameSproutApi * const api)  {

}

void Runtime::Update(GameSproutApi * const api)  {

}

void Runtime::Release(GameSproutApi * const api)  {

}
