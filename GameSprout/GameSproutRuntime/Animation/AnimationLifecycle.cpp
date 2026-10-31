#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Components/AnimatorComponent.h"
namespace GameSproutRuntime {
void Runtime::ForgetObjectAnimationBindings(std::uint64_t id){
    if(_ShuttingDown)return;
    for(const auto& entry:_Nodes){if(!IsUsable(entry.second))continue;
        if(auto* animator=dynamic_cast<AnimatorComponent*>(entry.second->Object->GetComponent<IAnimatorComponent>()))animator->ForgetObject(id);
        if(auto* skin=entry.second->Object->GetComponent<ISkinnedMeshRendererComponent>())if(skin->GetAnimatorObjectID()==id)skin->SetAnimatorObjectID(0);
    }
}
void Runtime::ForgetAnimatorReferences(std::uint64_t id){
    if(_ShuttingDown)return;
    for(const auto& entry:_Nodes){if(!IsUsable(entry.second))continue;
        if(auto* skin=entry.second->Object->GetComponent<ISkinnedMeshRendererComponent>())if(skin->GetAnimatorObjectID()==id)skin->SetAnimatorObjectID(0);
    }
}
}
