#include "BuildSetting.h"

void BuildSetting::BeforeInitialize(GameSproutApi * const api) {
    g_Self = this;
}

bool BuildSetting::Initialize(GameSproutApi * const api) {
    g_GameSproutApi = api;
    return true;
}

bool BuildSetting::Launch(GameSproutApi * const api)  {
    return true;
}

void BuildSetting::LaunchFinished(GameSproutApi * const api)  {

}

void BuildSetting::Update(GameSproutApi * const api)  {

}

void BuildSetting::Release(GameSproutApi * const api)  {

}
