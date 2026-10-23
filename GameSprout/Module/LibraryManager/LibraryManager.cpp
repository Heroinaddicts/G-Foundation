#include "LibraryManager.h"

void LibraryManager::BeforeInitialize(GameSproutApi * const api) {
    g_Self = this;
}

bool LibraryManager::Initialize(GameSproutApi * const api) {
    g_GameSproutApi = api;
    return true;
}

bool LibraryManager::Launch(GameSproutApi * const api)  {
    return true;
}

void LibraryManager::LaunchFinished(GameSproutApi * const api)  {

}

void LibraryManager::Update(GameSproutApi * const api)  {
    
}

void LibraryManager::Release(GameSproutApi * const api)  {
    
}
