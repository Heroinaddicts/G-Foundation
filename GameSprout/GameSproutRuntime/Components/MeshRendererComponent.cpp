#include "MeshRendererComponent.h"
#include "ComponentFactory.h"
#include <utility>

namespace GameSproutRuntime {
MeshRendererComponent::MeshRendererComponent(IGameObject* owner)
    : BuiltinComponent<IMeshRendererComponent>(owner, "GameSproutRuntime.MeshRenderer") {}
ResourceReference MeshRendererComponent::GetMaterial(std::size_t slot) const {
    return slot < _Materials.size() ? _Materials[slot] : ResourceReference{};
}
bool MeshRendererComponent::SetMaterial(std::size_t slot, const ResourceReference& material) {
    // 允许替换已有槽位或在末尾追加，不接受造成巨大稀疏数组的下标。
    if (slot > _Materials.size() || slot >= 1024) return false;
    if (slot < _Materials.size() && _Materials[slot] == material) return true;
    try {
        if (slot == _Materials.size()) _Materials.push_back(material);
        else {
            ResourceReference replacement = material;
            std::swap(_Materials[slot], replacement);
        }
    } catch (...) { return false; }
    NotifyComponentChanged(_GameObject, this);
    return true;
}
bool MeshRendererComponent::SetMaterials(const std::vector<ResourceReference>& materials) {
    if (materials.size() > 1024) return false;
    if (_Materials == materials) return true;
    try {
        std::vector<ResourceReference> replacement = materials;
        _Materials.swap(replacement);
    } catch (...) { return false; }
    NotifyComponentChanged(_GameObject, this);
    return true;
}
void MeshRendererComponent::SetVisible(bool value) {
    if (value == _Visible) return;
    _Visible = value;
    NotifyComponentChanged(_GameObject, this);
}
}
