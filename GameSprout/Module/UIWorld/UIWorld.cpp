#include "UIWorld.h"

bool UIWorld::Initialize(GameSproutModuleApi * const api) {
    g_GameSproutModuleApi = api;
    return true;
}

bool UIWorld::Launch(GameSproutModuleApi * const api)  {
    return true;
}

void UIWorld::LaunchFinished(GameSproutModuleApi * const api)  {

}

void UIWorld::Update(GameSproutModuleApi * const api)  {
    
}

void UIWorld::Release(GameSproutModuleApi * const api)  {
    
}
