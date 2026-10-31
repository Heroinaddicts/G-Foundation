#pragma once
#include "BuiltinComponent.h"

namespace GameSproutRuntime {
class MeshComponent final : public BuiltinComponent<IMeshComponent> {
public:
    explicit MeshComponent(IGameObject* owner);
    ResourceReference GetMesh() const override { return _Mesh; }
    void SetMesh(const ResourceReference& value) override;
    Bounds GetLocalBounds() const override { return _Bounds; }

private:
    friend class Renderer;
    Bounds _Bounds; // Renderer 加载产物后填充的派生缓存，不发场景修改事件，不参与保存。
    ResourceReference _Mesh;
};
}
