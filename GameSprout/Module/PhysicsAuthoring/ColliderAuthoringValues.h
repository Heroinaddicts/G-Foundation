#pragma once
#include "IPhysicsAuthoring.h"
#include "ISceneManager.h"

namespace ColliderAuthoringValues {
bool Read(const GameSproutRuntime::IColliderComponent&,ColliderEditState&);
bool Equal(const ColliderEditState&,const ColliderEditState&);
bool Write(GameSproutRuntime::IColliderComponent&,const ColliderEditState&,std::string&);
ISceneManager::fSceneEditOperation Operation(ISceneManager*,std::uint64_t fileID,std::uint64_t liveID,const ColliderEditState&);
}
