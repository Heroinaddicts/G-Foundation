#pragma once
#include "BuiltinComponent.h"

namespace GameSproutRuntime {
class CanvasComponent final : public BuiltinComponent<ICanvasComponent> {
public:
    explicit CanvasComponent(IGameObject* owner);
    CanvasRenderMode GetRenderMode() const override { return _Mode; }
    void SetRenderMode(CanvasRenderMode value) override;
    Vector2 GetReferenceResolution() const override { return _Resolution; }
    bool SetReferenceResolution(Vector2 value) override;
    float GetPixelsPerUnit() const override { return _PixelsPerUnit; }
    bool SetPixelsPerUnit(float value) override;

private:
    CanvasRenderMode _Mode = CanvasRenderMode::ScreenSpace;
    Vector2 _Resolution{1920.0f, 1080.0f};
    float _PixelsPerUnit = 100.0f;
};
}
