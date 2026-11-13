#pragma once
#include "BuiltinComponent.h"
#include <vector>

namespace GameSproutRuntime {
class MeshRendererComponent final : public BuiltinComponent<IMeshRendererComponent> {
public:
    explicit MeshRendererComponent(IGameObject* owner);
    std::size_t GetMaterialCount() const override { return _Materials.size(); }
    ResourceReference GetMaterial(std::size_t slot) const override;
    bool SetMaterial(std::size_t slot, const ResourceReference& material) override;
    bool SetMaterials(const std::vector<ResourceReference>& materials) override;
    void SetVisible(bool value) override;
    bool IsVisible() const override { return _Visible; }
    ShadowCastingMode GetShadowCastingMode() const override { return _ShadowCastingMode; }
    bool SetShadowCastingMode(ShadowCastingMode mode) override;
    bool GetReceiveShadows() const override { return _ReceiveShadows; }
    void SetReceiveShadows(bool enabled) override;

private:
    std::vector<ResourceReference> _Materials;
    ShadowCastingMode _ShadowCastingMode = ShadowCastingMode::On;
    bool _Visible = true;
    bool _ReceiveShadows = true;
};
}
