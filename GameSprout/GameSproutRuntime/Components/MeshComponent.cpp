#include "MeshComponent.h"
#include "ComponentFactory.h"

namespace GameSproutRuntime {
MeshComponent::MeshComponent(IGameObject* owner)
    : BuiltinComponent<IMeshComponent>(owner, "GameSproutRuntime.Mesh") {}
void MeshComponent::SetMesh(const ResourceReference& value) {
    if (_Mesh == value) return;
    _Mesh = value;
    _Bounds={};
    NotifyComponentChanged(_GameObject, this);
}
}
