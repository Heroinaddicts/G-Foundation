#include "UIAuthoring.h"
#include <cmath>

namespace {
using GameSproutRuntime::IGameObject;
using GameSproutRuntime::IRectTransformComponent;
using GameSproutRuntime::Quaternion;
using GameSproutRuntime::Vector2;

IRectTransformComponent* FindRect(IGameObject& object, std::uint64_t instanceID) {
    auto* rect = object.GetComponent<IRectTransformComponent>();
    return rect && rect->GetComponentInstanceID() == instanceID ? rect : nullptr;
}

bool SameRect(const UIRectAuthoringValues& a, const UIRectAuthoringValues& b) {
    if (a.AnchoredPosition.X != b.AnchoredPosition.X || a.AnchoredPosition.Y != b.AnchoredPosition.Y ||
        a.SizeDelta.X != b.SizeDelta.X || a.SizeDelta.Y != b.SizeDelta.Y) return false;
    const auto x = a.LocalRotation;
    const auto y = b.LocalRotation;
    return (x.X == y.X && x.Y == y.Y && x.Z == y.Z && x.W == y.W) ||
        (x.X == -y.X && x.Y == -y.Y && x.Z == -y.Z && x.W == -y.W);
}

ISceneManager::fSceneEditOperation RectOperation(ISceneManager& scenes, std::uint64_t fileID,
    std::uint64_t instanceID, UIRectAuthoringValues value) {
    return [scenes = &scenes, fileID, instanceID, value](IGameObject& object, std::string& error) {
        auto* rect = fileID ? dynamic_cast<IRectTransformComponent*>(
            scenes->QueryComponentByFileID(object.GetInstanceID(), fileID)) : FindRect(object, instanceID);
        if (!rect) {
            error = "RectTransform 已移除或所属文档已切换。";
            return false;
        }
        auto* transform = object.GetTransform();
        if (!transform->SetLocalRotation(value.LocalRotation)) {
            error = "UI 旋转值无效。";
            return false;
        }
        rect->SetAnchoredPosition(value.AnchoredPosition);
        rect->SetSizeDelta(value.SizeDelta);
        return true;
    };
}

bool Finite(Vector2 value) { return std::isfinite(value.X) && std::isfinite(value.Y); }
}

bool UIAuthoring::HasRectEdit() const {
    return _Editing && _RectEdit.Target.ComponentID && _Editing->IsContinuousEditFor(_RectEdit.Target);
}

bool UIAuthoring::BeginRectEdit(const ObjectEditTarget& target, std::string& error) {
    if (!_Runtime || !_Scenes || !_Editing) {
        error = "UI 编辑服务未就绪。";
        return false;
    }
    if (!_Editing->CanEdit(target, error)) return false;
    auto* object = _Runtime->QueryGameObject(target.ObjectID);
    auto* rect = object ? FindRect(*object, target.ComponentID) : nullptr;
    auto* canvas = rect ? GameSproutRuntime::FindUIScreenCanvasRoot(*object) : nullptr;
    if (!canvas || object == canvas) {
        error = "只能在 Scene 中拖拽屏幕空间 Canvas 下的 RectTransform。";
        return false;
    }

    const UIRectAuthoringValues before{rect->GetAnchoredPosition(), rect->GetSizeDelta(),
        object->GetTransform()->GetLocalRotation()};
    const bool playing = _Scenes->IsPlaySessionActive();
    const auto fileID = playing ? 0 : _Scenes->GetComponentFileID(target.ObjectID, target.ComponentID);
    if (!playing && !fileID) {
        error = "无法获取 RectTransform 的文档标识。";
        return false;
    }
    if (!_Editing->BeginContinuousEdit(target, "编辑 UI 矩形",
            RectOperation(*_Scenes, fileID, target.ComponentID, before), error)) return false;
    _RectEdit = {target, fileID, before, before};
    return true;
}

bool UIAuthoring::PreviewRectEdit(const UIRectAuthoringValues& next, std::string& error) {
    error.clear();
    if (!HasRectEdit()) {
        _RectEdit = {};
        error = "没有活动的 UI 矩形编辑。";
        return false;
    }
    Quaternion normalized;
    if (!Finite(next.AnchoredPosition) || !Finite(next.SizeDelta) || !next.LocalRotation.TryNormalize(normalized)) {
        error = "UI 矩形的位置、尺寸和旋转必须有效。";
        return false;
    }
    if (SameRect(next, _RectEdit.Last)) return true;
    const auto instanceID = _RectEdit.Target.ComponentID;
    if (!_Editing->PreviewContinuousEdit(RectOperation(*_Scenes, _RectEdit.FileID, instanceID, next),
            RectOperation(*_Scenes, _RectEdit.FileID, instanceID, _RectEdit.Last), error)) return false;
    _RectEdit.Last = next;
    return true;
}

bool UIAuthoring::CommitRectEdit(const UIRectAuthoringValues& next, std::string& error) {
    // Play edits are transient, so the last sample must be previewed as well.
    if (!PreviewRectEdit(next, error)) return false;
    if (SameRect(_RectEdit.Before, _RectEdit.Last)) return CancelRectEdit(error);
    const auto completed = _Editing->CommitContinuousEdit(
        RectOperation(*_Scenes, _RectEdit.FileID, _RectEdit.Target.ComponentID, _RectEdit.Last), error);
    if (completed || !HasRectEdit()) _RectEdit = {};
    return completed;
}

bool UIAuthoring::CancelRectEdit(std::string& error) {
    error.clear();
    const bool cancelled = !HasRectEdit() || _Editing->CancelContinuousEdit(error);
    if (cancelled || !HasRectEdit()) _RectEdit = {};
    return cancelled;
}
