#pragma once
#include "GameSproutRuntimeApi.h"
#include <array>
#include <cmath>
#include <string_view>

namespace GameSproutRuntime {
inline constexpr std::size_t MaximumUITextBytes = 65536;
inline constexpr float DefaultUIFontSize = 24.0f;
inline constexpr float MaximumUIFontSize = 4096.0f;

struct UIScreenPreviewGeometry {
    // Render-target pixels, top-left origin. Corners are ordered TL, TR, BR, BL
    // in layout space and retain that order after rotation or mirroring.
    Rect CanvasBounds;
    std::array<Vector2, 4> Corners{};
    Vector2 Pivot;
    // Render-target displacement for one authored layout unit to the right
    // or up. Parent axes omit this object's rotation and scale.
    Vector2 ParentRight, ParentUp;
    Vector2 Right, Up;
    Vector2 Size; // Untransformed width and height in layout units.
    bool IsCanvasRoot = false;
};

// Screen-space controls belong to their outermost screen canvas. A world-space
// Canvas stops the search, even when its parent is a screen canvas.
inline IGameObject* FindUIScreenCanvasRoot(IGameObject& object) {
    if (!object.GetComponent<IRectTransformComponent>() && !object.GetComponent<ICanvasComponent>()) return nullptr;
    IGameObject* root = nullptr;
    for (auto* node = &object; node; node = node->Parent()) {
        if (const auto* canvas = node->GetComponent<ICanvasComponent>()) {
            if (canvas->GetRenderMode() != CanvasRenderMode::ScreenSpace) break;
            root = node;
        }
    }
    return root;
}

inline bool IsValidUIFontSize(float size) {
    return std::isfinite(size) && size > 0.0f && size <= MaximumUIFontSize;
}

inline bool IsValidUIText(std::string_view text) {
    if (text.size() > MaximumUITextBytes) return false;
    for (std::size_t index = 0; index < text.size();) {
        const auto first = static_cast<unsigned char>(text[index++]);
        if (first < 0x80) continue;
        if (first < 0xC2 || first > 0xF4) return false;
        const std::size_t continuation = first < 0xE0 ? 1 : first < 0xF0 ? 2 : 3;
        if (text.size() - index < continuation) return false;
        const auto second = static_cast<unsigned char>(text[index]);
        if ((first == 0xE0 && second < 0xA0) || (first == 0xED && second > 0x9F) ||
            (first == 0xF0 && second < 0x90) || (first == 0xF4 && second > 0x8F)) return false;
        for (std::size_t byte = 0; byte < continuation; ++byte)
            if ((static_cast<unsigned char>(text[index++]) & 0xC0) != 0x80) return false;
    }
    return true;
}

class GAMESPROUT_RUNTIME_PUBLIC_TYPE ITextComponent : public IComponent {
public:
    // UTF-8. Invalid input leaves the previous text intact.
    virtual const std::string& GetText() const = 0;
    virtual bool SetText(const std::string& text) = 0;
    // Font resource identity. An empty reference uses the built-in fallback.
    virtual const ResourceReference& GetFont() const = 0;
    virtual void SetFont(const ResourceReference& font) = 0;
    virtual Color GetColor() const = 0;
    virtual void SetColor(Color color) = 0;
    // Canvas layout units. A positive finite value up to MaximumUIFontSize.
    virtual float GetFontSize() const = 0;
    virtual bool SetFontSize(float size) = 0;
protected:
    using IComponent::IComponent;
};

class GAMESPROUT_RUNTIME_PUBLIC_TYPE IButtonComponent : public IComponent {
public:
    using fClickCallback = std::function<void()>;
    virtual bool GetInteractable() const = 0;
    virtual void SetInteractable(bool interactable) = 0;
    virtual bool IsPressed() const = 0;
    // Host thread. Dispatch occurs in the gameplay frame after a matching
    // left-button press and release. Zero means registration failed.
    // Script-owned subscriptions are removed when their native instance stops.
    // Other callers must unregister before their callback's captures expire.
    virtual std::uint64_t RegisterClick(const fClickCallback& callback) = 0;
    virtual bool UnregisterClick(std::uint64_t token) = 0;
protected:
    using IComponent::IComponent;
};
}
