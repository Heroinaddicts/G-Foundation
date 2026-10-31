#pragma once
#include "BuiltinComponent.h"

namespace GameSproutRuntime {
class ImageComponent final : public BuiltinComponent<IImageComponent> {
public:
    explicit ImageComponent(IGameObject* owner);
    ResourceReference GetTexture() const override { return _Texture; }
    void SetTexture(const ResourceReference& value) override;
    Color GetColor() const override { return _Color; }
    void SetColor(Color value) override;
    Rect GetUVRect() const override { return _UV; }
    void SetUVRect(const Rect& value) override;

private:
    ResourceReference _Texture;
    Color _Color;
    Rect _UV{{0.0f, 0.0f}, {1.0f, 1.0f}};
};
}
