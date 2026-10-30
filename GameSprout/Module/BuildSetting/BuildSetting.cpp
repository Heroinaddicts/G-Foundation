#include "BuildSetting.h"

void BuildSetting::BeforeInitialize(GameSproutModuleApi * const api) {
    g_Self = this;
}

bool BuildSetting::Initialize(GameSproutModuleApi * const api) {
    g_GameSproutModuleApi = api;
    return true;
}

bool BuildSetting::Launch(GameSproutModuleApi * const api)  {
    return true;
}

void BuildSetting::LaunchFinished(GameSproutModuleApi * const api)  {

}

void BuildSetting::Update(GameSproutModuleApi * const api)  {

}

void BuildSetting::Release(GameSproutModuleApi * const api)  {

}
