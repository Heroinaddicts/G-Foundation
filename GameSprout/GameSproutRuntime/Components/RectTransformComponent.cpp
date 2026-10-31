#include "RectTransformComponent.h"
#include "ComponentFactory.h"
#include <cmath>

namespace GameSproutRuntime {
static bool Finite(Vector2 value) { return std::isfinite(value.X) && std::isfinite(value.Y); }
static bool Normalized(Vector2 value) { return Finite(value) && value.X >= 0 && value.X <= 1 && value.Y >= 0 && value.Y <= 1; }
static bool Equal(Vector2 a, Vector2 b) { return a.X == b.X && a.Y == b.Y; }

RectTransformComponent::RectTransformComponent(IGameObject* owner)
    : BuiltinComponent<IRectTransformComponent>(owner, "GameSproutRuntime.RectTransform") {}

void RectTransformComponent::SetAnchors(Vector2 minimum, Vector2 maximum) {
    if (!Normalized(minimum) || !Normalized(maximum) || minimum.X > maximum.X || minimum.Y > maximum.Y ||
        (Equal(minimum, _AnchorMin) && Equal(maximum, _AnchorMax))) return;
    _AnchorMin = minimum;
    _AnchorMax = maximum;
    NotifyComponentChanged(_GameObject, this);
}
void RectTransformComponent::SetPivot(Vector2 value) {
    if (!Normalized(value) || Equal(value, _Pivot)) return;
    _Pivot = value;
    NotifyComponentChanged(_GameObject, this);
}
void RectTransformComponent::SetAnchoredPosition(Vector2 value) {
    if (!Finite(value) || Equal(value, _Position)) return;
    _Position = value;
    NotifyComponentChanged(_GameObject, this);
}
void RectTransformComponent::SetSizeDelta(Vector2 value) {
    // 拉伸锚点下的尺寸增量允许负值。
    if (!Finite(value) || Equal(value, _SizeDelta)) return;
    _SizeDelta = value;
    NotifyComponentChanged(_GameObject, this);
}
}
