#include "UIComponents.h"
#include "Components/ComponentFactory.h"
#include "Components/ScriptComponent.h"
#include "GameObject.h"
#include "RuntimeMath.h"
#include "Scene.h"
#include <limits>

namespace GameSproutRuntime {
TextComponent::TextComponent(IGameObject* owner)
    : BuiltinComponent<ITextComponent>(owner, "GameSproutRuntime.Text") {}

bool TextComponent::SetText(const std::string& text) {
    if (!IsValidUIText(text)) return false;
    if (_Text != text) {
        _Text = text;
        NotifyComponentChanged(_GameObject, this);
    }
    return true;
}

void TextComponent::SetFont(const ResourceReference& font) {
    if (_Font == font) return;
    _Font = font;
    NotifyComponentChanged(_GameObject, this);
}

void TextComponent::SetColor(Color color) {
    if (!RuntimeMath::IsFinite(color) || color.R < 0 || color.G < 0 || color.B < 0 ||
        color.A < 0 || color.A > 1 ||
        (color.R == _Color.R && color.G == _Color.G && color.B == _Color.B && color.A == _Color.A)) return;
    _Color = color;
    NotifyComponentChanged(_GameObject, this);
}

bool TextComponent::SetFontSize(float size) {
    if (!IsValidUIFontSize(size)) return false;
    if (_FontSize != size) {
        _FontSize = size;
        NotifyComponentChanged(_GameObject, this);
    }
    return true;
}

ButtonComponent::ButtonComponent(IGameObject* owner)
    : BuiltinComponent<IButtonComponent>(owner, "GameSproutRuntime.Button") {}

void ButtonComponent::OnCreate(IGameSproutRuntime* runtime) {
    _Runtime = static_cast<Runtime*>(runtime);
    _Runtime->RegisterUIButton(this);
}

void ButtonComponent::OnRelease(IGameSproutRuntime*) {
    _Runtime->UnregisterUIButton(this);
    _Subscriptions.clear();
}

void ButtonComponent::SetInteractable(bool interactable) {
    if (_Interactable == interactable) return;
    _Interactable = interactable;
    if (!interactable) SetPressed(false);
    NotifyComponentChanged(_GameObject, this);
}

void ButtonComponent::SetPressed(bool pressed) {
    if (_Pressed == pressed) return;
    _Pressed = pressed;
    if (auto* scene = _GameObject->GetScene()) static_cast<Scene*>(scene)->InvalidateUI();
}

std::uint64_t ButtonComponent::RegisterClick(const fClickCallback& callback) {
    if (!callback || _NextToken == std::numeric_limits<std::uint64_t>::max() ||
        !_Runtime->IsUsable(_Runtime->Find(_GameObject))) return 0;
    Subscription subscription;
    subscription.Callback = callback;
    if (auto* script = _Runtime->GetInvokingScript()) {
        subscription.ObjectID = script->GetGameObject()->GetInstanceID();
        subscription.ComponentID = script->GetComponentInstanceID();
    }
    _Subscriptions.emplace(++_NextToken, std::move(subscription));
    return _NextToken;
}

bool ButtonComponent::UnregisterClick(std::uint64_t token) {
    return _Subscriptions.erase(token) != 0;
}

void ButtonComponent::DropSubscriptions(std::uint64_t object, std::uint64_t component) noexcept {
    std::erase_if(_Subscriptions, [object, component](const auto& entry) {
        return entry.second.ObjectID == object && entry.second.ComponentID == component;
    });
}

void ButtonComponent::DispatchClick() {
    const auto last = _NextToken;
    std::uint64_t previous = 0;
    while (_Runtime->FindUIButton(_GameObject->GetInstanceID(), GetComponentInstanceID()) == this &&
           _Interactable && _GameObject->GetScene() && _GameObject->IsActiveInHierarchy()) {
        const auto next = _Subscriptions.upper_bound(previous);
        if (next == _Subscriptions.end() || next->first > last) break;
        previous = next->first;
        try {
            // Copy before invoking, so callbacks can remove or add subscriptions.
            const auto subscription = next->second;
            if (!subscription.ObjectID) {
                subscription.Callback();
                continue;
            }
            auto* owner = _Runtime->Find(subscription.ObjectID);
            if (!_Runtime->IsInScene(owner) || !owner->IsActiveInHierarchy()) continue;
            for (const auto& component : owner->Components) {
                if (component.PendingRemove || component.Serial != subscription.ComponentID) continue;
                if (auto* script = dynamic_cast<ScriptComponent*>(component.Pointer))
                    script->InvokeExternal(subscription.Callback);
                break;
            }
        } catch (const std::exception& error) {
            _Runtime->NotifyScriptError(_GameObject, error.what());
        } catch (...) {
            _Runtime->NotifyScriptError(_GameObject, "UI click subscriber failed");
        }
    }
}
}
