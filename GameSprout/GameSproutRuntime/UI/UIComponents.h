#pragma once
#include "Components/BuiltinComponent.h"
#include <map>

namespace GameSproutRuntime {
class Runtime;

class TextComponent final : public BuiltinComponent<ITextComponent> {
public:
    explicit TextComponent(IGameObject* owner);
    const std::string& GetText() const override { return _Text; }
    bool SetText(const std::string& text) override;
    const ResourceReference& GetFont() const override { return _Font; }
    void SetFont(const ResourceReference& font) override;
    Color GetColor() const override { return _Color; }
    void SetColor(Color color) override;
    float GetFontSize() const override { return _FontSize; }
    bool SetFontSize(float size) override;
private:
    std::string _Text;
    ResourceReference _Font;
    Color _Color;
    float _FontSize = DefaultUIFontSize;
};

class ButtonComponent final : public BuiltinComponent<IButtonComponent> {
public:
    explicit ButtonComponent(IGameObject* owner);
    bool GetInteractable() const override { return _Interactable; }
    void SetInteractable(bool interactable) override;
    bool IsPressed() const override { return _Pressed; }
    std::uint64_t RegisterClick(const fClickCallback& callback) override;
    bool UnregisterClick(std::uint64_t token) override;

    void SetPressed(bool pressed);
    void DispatchClick();
    void DropSubscriptions(std::uint64_t object, std::uint64_t component) noexcept;
protected:
    void OnCreate(IGameSproutRuntime*) override;
    void OnRelease(IGameSproutRuntime*) override;
private:
    struct Subscription {
        std::uint64_t ObjectID = 0, ComponentID = 0;
        fClickCallback Callback;
    };
    Runtime* _Runtime = nullptr;
    bool _Interactable = true;
    bool _Pressed = false;
    std::uint64_t _NextToken = 0;
    std::map<std::uint64_t, Subscription> _Subscriptions;
};
}
