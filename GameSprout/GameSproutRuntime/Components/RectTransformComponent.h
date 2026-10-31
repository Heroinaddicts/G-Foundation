#pragma once
#include "BuiltinComponent.h"

namespace GameSproutRuntime {
class RectTransformComponent final : public BuiltinComponent<IRectTransformComponent> {
public:
    explicit RectTransformComponent(IGameObject* owner);
    Vector2 GetAnchorMin() const override { return _AnchorMin; }
    Vector2 GetAnchorMax() const override { return _AnchorMax; }
    void SetAnchors(Vector2 minimum, Vector2 maximum) override;
    Vector2 GetPivot() const override { return _Pivot; }
    void SetPivot(Vector2 value) override;
    Vector2 GetAnchoredPosition() const override { return _Position; }
    void SetAnchoredPosition(Vector2 value) override;
    Vector2 GetSizeDelta() const override { return _SizeDelta; }
    void SetSizeDelta(Vector2 value) override;

private:
    Vector2 _AnchorMin{0.5f, 0.5f};
    Vector2 _AnchorMax{0.5f, 0.5f};
    Vector2 _Pivot{0.5f, 0.5f};
    Vector2 _Position;
    Vector2 _SizeDelta{100.0f, 100.0f};
};
}
