#include "RuntimeUILayout.h"
#include "UIComponents.h"
#include "GameObject.h"
#include "Scene.h"
#include <cmath>

namespace GameSproutRuntime {
void Runtime::SetUIInputViewport(IScene* scene, Vector2 size, Vector2 renderedSize) {
    std::uint64_t id = 0;
    if (auto* node = Find(scene); IsInScene(node) && node->IsSceneRoot() &&
        std::isfinite(size.X) && std::isfinite(size.Y) && size.X > 0 && size.Y > 0) id = node->ID;
    if (!id) size = {};
    if (!id || !std::isfinite(renderedSize.X) || !std::isfinite(renderedSize.Y) ||
        renderedSize.X <= 0 || renderedSize.Y <= 0) renderedSize = size;
    if (id == _UIInputSceneID && size.X == _UIInputSize.X && size.Y == _UIInputSize.Y &&
        renderedSize.X == _UIRenderSize.X && renderedSize.Y == _UIRenderSize.Y) return;
    ResetUIPointer();
    _UIInputSceneID = id;
    _UIInputSize = size;
    _UIRenderSize = renderedSize;
}

ButtonComponent* Runtime::FindUIButton(std::uint64_t objectID, std::uint64_t componentID) const {
    auto* node = Find(objectID);
    if (!IsUsable(node)) return nullptr;
    for (const auto& component : node->Components)
        if (component.Serial == componentID && !component.PendingRemove)
            return dynamic_cast<ButtonComponent*>(component.Pointer);
    return nullptr;
}

void Runtime::DropUISubscriptions(std::uint64_t objectID, std::uint64_t componentID) noexcept {
    for (auto* button : _UIButtons) button->DropSubscriptions(objectID, componentID);
}

void Runtime::ResetUIPointer() {
    _UIPointerEdges.clear();
    if (auto* button = FindUIButton(_UIPressedObjectID, _UIPressedComponentID)) button->SetPressed(false);
    _UIPressedObjectID = _UIPressedComponentID = 0;
}

void Runtime::QueueUIPointer(bool down) {
    if (!_UIInputSceneID || !_Input.HasMousePosition) return;
    _UIPointerEdges.push_back({_Input.MousePosition, down});
}

void Runtime::DispatchUIPointer() {
    if (_UIPressedObjectID) {
        auto* button = FindUIButton(_UIPressedObjectID, _UIPressedComponentID);
        auto* scene = button ? button->GetGameObject()->GetScene() : nullptr;
        if (!scene || scene->GetInstanceID() != _UIInputSceneID || !button->GetGameObject()->IsActiveInHierarchy()) {
            if (button) button->SetPressed(false);
            _UIPressedObjectID = _UIPressedComponentID = 0;
        }
    }
    if (_UIPointerEdges.empty()) return;
    std::vector<UIPointerEdge> batch;
    batch.swap(_UIPointerEdges);
    const auto sceneID = _UIInputSceneID;
    for (const auto& edge : batch) {
        auto* root = Find(sceneID);
        if (!_InputEnabled || !_Input.Focused || _UIInputSceneID != sceneID ||
            !IsInScene(root) || !root->SceneOwner->IsSimulationEnabled()) {
            ResetUIPointer();
            return;
        }

        ButtonComponent* hit = nullptr;
        if (!_UIButtons.empty() && ContainsUIPoint({{}, _UIInputSize}, edge.Position)) {
            const Vector2 pixel{(edge.Position.X / _UIInputSize.X) * _UIRenderSize.X,
                                (edge.Position.Y / _UIInputSize.Y) * _UIRenderSize.Y};
            const auto layout = BuildUIFrame(*this, *root->SceneOwner, _UIRenderSize);
            for (auto element = layout.rbegin(); element != layout.rend(); ++element) {
                auto* button = element->Object->GetComponent<IButtonComponent>();
                if (button && HitUIElement(*element, pixel)) {
                    hit = FindUIButton(element->Object->GetInstanceID(), button->GetComponentInstanceID());
                    if (hit) break;
                }
            }
        }
        auto* pressed = FindUIButton(_UIPressedObjectID, _UIPressedComponentID);
        if (edge.Down) {
            if (pressed) pressed->SetPressed(false);
            _UIPressedObjectID = _UIPressedComponentID = 0;
            if (hit && hit->GetInteractable()) {
                _UIPressedObjectID = hit->GetGameObject()->GetInstanceID();
                _UIPressedComponentID = hit->GetComponentInstanceID();
                hit->SetPressed(true);
            }
        } else {
            _UIPressedObjectID = _UIPressedComponentID = 0;
            if (!pressed) continue;
            const bool click = pressed == hit && pressed->IsPressed() && pressed->GetInteractable();
            pressed->SetPressed(false);
            if (click) {
                RuntimeOperation operation(*this);
                FrameComponent record{pressed->GetGameObject()->GetInstanceID(), pressed->GetComponentInstanceID()};
                const auto constructionDepth = _ConstructionDepth;
                pressed->DispatchClick();
                RecoverCallbackConstruction(record, *pressed, constructionDepth);
            }
        }
    }
}
}
