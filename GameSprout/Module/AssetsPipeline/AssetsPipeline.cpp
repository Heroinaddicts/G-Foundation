#include "AssetsPipeline.h"

void AssetsPipeline::BeforeInitialize(GameSproutApi * const api) {
    g_Self = this;
}

bool AssetsPipeline::Initialize(GameSproutApi * const api) {
    g_GameSproutApi = api;
    return true;
}

bool AssetsPipeline::Launch(GameSproutApi * const api)  {
    return true;
}

void AssetsPipeline::LaunchFinished(GameSproutApi * const api)  {

}

void AssetsPipeline::Update(GameSproutApi * const api)  {
    
}

void AssetsPipeline::Release(GameSproutApi * const api)  {
    
}
