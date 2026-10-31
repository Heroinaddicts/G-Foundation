#include "CanvasComponent.h"
#include "ComponentFactory.h"
#include <cmath>

namespace GameSproutRuntime {
CanvasComponent::CanvasComponent(IGameObject* owner)
    : BuiltinComponent<ICanvasComponent>(owner, "GameSproutRuntime.Canvas") {}
void CanvasComponent::SetRenderMode(CanvasRenderMode value) {
    if ((value != CanvasRenderMode::ScreenSpace && value != CanvasRenderMode::WorldSpace) || value == _Mode) return;
    _Mode = value;
    NotifyComponentChanged(_GameObject, this);
}
bool CanvasComponent::SetReferenceResolution(Vector2 value) {
    if (!std::isfinite(value.X) || !std::isfinite(value.Y) || value.X <= 0 || value.Y <= 0) return false;
    if (value.X == _Resolution.X && value.Y == _Resolution.Y) return true;
    _Resolution = value;
    NotifyComponentChanged(_GameObject, this);
    return true;
}
bool CanvasComponent::SetPixelsPerUnit(float value) {
    if (!std::isfinite(value) || value <= 0) return false;
    if (value == _PixelsPerUnit) return true;
    _PixelsPerUnit = value;
    NotifyComponentChanged(_GameObject, this);
    return true;
}
}
