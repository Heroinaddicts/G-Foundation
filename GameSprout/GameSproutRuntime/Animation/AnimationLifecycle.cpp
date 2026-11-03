#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Components/AnimatorComponent.h"
namespace GameSproutRuntime {
void Runtime::ForgetObjectAnimationBindings(std::uint64_t id){
    if(_ShuttingDown)return;
    for(const auto& [objectID,count]:_AnimationObjects){auto* node=Find(objectID);if(!IsUsable(node))continue;
        if(auto* animator=dynamic_cast<AnimatorComponent*>(node->Object->GetComponent<IAnimatorComponent>()))animator->ForgetObject(id);
        if(auto* skin=node->Object->GetComponent<ISkinnedMeshRendererComponent>())if(skin->GetAnimatorObjectID()==id)skin->SetAnimatorObjectID(0);
    }
}
void Runtime::ForgetAnimatorReferences(std::uint64_t id){
    if(_ShuttingDown)return;
    for(const auto& [objectID,count]:_AnimationObjects){auto* node=Find(objectID);if(!IsUsable(node))continue;
        if(auto* skin=node->Object->GetComponent<ISkinnedMeshRendererComponent>())if(skin->GetAnimatorObjectID()==id)skin->SetAnimatorObjectID(0);
    }
}
}
