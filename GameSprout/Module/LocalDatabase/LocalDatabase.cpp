#include "LocalDatabase.h"
#include "LDBSession.h"

ILDBSession * LocalDatabase::CreateLDB(const std::string & path) {
    return LDBSession::Create(path);
}

ILDBSession * LocalDatabase::OpenLDB(const std::string & path) {
    return LDBSession::Open(path);
}

void LocalDatabase::BeforeInitialize(GameSproutModuleApi * const api) {
    g_Self = this;
}

bool LocalDatabase::Initialize(GameSproutModuleApi * const api) {
    g_GameSproutModuleApi = api;
    return true;
}

bool LocalDatabase::Launch(GameSproutModuleApi * const api)  {
    return true;
}

void LocalDatabase::LaunchFinished(GameSproutModuleApi * const api)  {

}

void LocalDatabase::Update(GameSproutModuleApi * const api)  {
    
}

void LocalDatabase::Release(GameSproutModuleApi * const api)  {
    
}
