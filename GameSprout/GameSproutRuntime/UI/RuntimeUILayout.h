#pragma once
#include "Api/GameSproutRuntimeApi.h"

namespace GameSproutRuntime {
class Runtime;

// Maps the unrotated, top-left-origin layout plane into render-target pixels.
struct UILayoutTransform {
    Vector2 Offset;
    Vector2 X{1, 0}, Y{0, 1};

    Vector2 Direction(Vector2 value) const { return X * value.X + Y * value.Y; }
    Vector2 Point(Vector2 value) const { return Offset + Direction(value); }
    bool TryInversePoint(Vector2 value, Vector2& output) const;
};

struct UIElement {
    IGameObject* Object = nullptr;
    Rect Rectangle; // Untransformed, top-left origin, in viewport units.
    float Scale = 1.0f; // Canvas layout units to viewport units.
    Vector2 Pivot;
    UILayoutTransform ParentTransform;
    UILayoutTransform Transform;
};

// Returns a transient back-to-front snapshot of active ScreenSpace Canvas
// elements. Its object pointers are borrowed only for immediate layout consumers.
std::vector<UIElement> BuildUIFrame(const Runtime&, IScene&, Vector2 viewportSize);

struct UIPreviewFrame {
    Rect Canvas;
    std::vector<UIElement> Elements;
    bool IsCanvasSelected = false;
};

inline bool ContainsUIPoint(const Rect& rectangle, Vector2 point) {
    return point.X >= rectangle.Position.X && point.Y >= rectangle.Position.Y &&
        point.X < rectangle.Position.X + rectangle.Size.X &&
        point.Y < rectangle.Position.Y + rectangle.Size.Y;
}

bool HitUIElement(const UIElement&, Vector2 point);
UIPreviewFrame BuildUIPreview(const Runtime&, IScene&, std::uint64_t objectID, Vector2 viewportSize, Vector2 gameViewportSize);
}
