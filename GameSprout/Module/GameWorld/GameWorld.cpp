#include "GameWorld.h"

bool GameWorld::Initialize(GameSproutApi * const api) {
    g_GameSproutApi = api;
    return true;
}

bool GameWorld::Launch(GameSproutApi * const api)  {
    return true;
}

void GameWorld::LaunchFinished(GameSproutApi * const api)  {

}

void GameWorld::Update(GameSproutApi * const api)  {
    
}

void GameWorld::Release(GameSproutApi * const api)  {
    
}
