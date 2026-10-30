#include "GameWorld.h"

bool GameWorld::Initialize(GameSproutModuleApi * const api) {
    g_GameSproutModuleApi = api;
    return true;
}

bool GameWorld::Launch(GameSproutModuleApi * const api)  {
    return true;
}

void GameWorld::LaunchFinished(GameSproutModuleApi * const api)  {

}

void GameWorld::Update(GameSproutModuleApi * const api)  {
    
}

void GameWorld::Release(GameSproutModuleApi * const api)  {
    
}
