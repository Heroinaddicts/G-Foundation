#include "LocalDatabase.h"
#include "LDBSession.h"

ILDBSession * LocalDatabase::CreateLDB(const std::string & path) {
    return LDBSession::Create(path);
}

ILDBSession * LocalDatabase::OpenLDB(const std::string & path) {
    return LDBSession::Open(path);
}

void LocalDatabase::BeforeInitialize(GameSproutApi * const api) {
    g_Self = this;
}

bool LocalDatabase::Initialize(GameSproutApi * const api) {
    g_GameSproutApi = api;
    return true;
}

bool LocalDatabase::Launch(GameSproutApi * const api)  {
    return true;
}

void LocalDatabase::LaunchFinished(GameSproutApi * const api)  {

}

void LocalDatabase::Update(GameSproutApi * const api)  {
    
}

void LocalDatabase::Release(GameSproutApi * const api)  {
    
}
