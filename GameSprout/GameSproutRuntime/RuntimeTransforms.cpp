#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Components/TransformComponent.h"

namespace GameSproutRuntime {
void Runtime::InvalidateTransforms() {
    if(++_TransformEpoch)return;
    // The counter is practically inexhaustible, but wrapping may never revive
    // an ancient cache entry. Only the wrap path touches all registered nodes.
    _TransformEpoch=1;
    for(const auto& entry:_Nodes)
        if(auto* transform=dynamic_cast<TransformComponent*>(entry.second->Object->GetTransform())) {
            transform->_WorldMatrixEpoch=0;
            transform->_WorldMatrixChangeEpoch=0;
        }
}
}
