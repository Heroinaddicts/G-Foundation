#pragma once
#include "ComponentEditor.h"
namespace Ui { class TransformComponentEditorForm; }
class TransformComponentEditor final : public ComponentEditor, public IAnimationPreviewListener {
public:
    explicit TransformComponentEditor(QWidget* parent);
    ~TransformComponentEditor() override;
    void OnAnimationPreviewStateChanged(bool) override;
    void OnAnimationPreviewReset() override;
    void OnClipPreviewStateChanged(uint64_t, const GameSproutRuntime::AnimationClipPlaybackState&) override;
    void OnAnimationEvent(const GameSproutRuntime::AnimationNotification&) override;
private:
    void ContextChanged() override;
    void ReadValues(GameSproutRuntime::IComponent& component) override;
    void CommitValues(bool rotationEdited);
    void RefreshAnimationState();
    Ui::TransformComponentEditorForm* _Ui = nullptr;
    GameSproutRuntime::Quaternion _DisplayedRotation;
    GameSproutRuntime::Vector3 _DisplayedEuler;
    bool _HasDisplayedRotation=false;
    IAnimationPreview* _Preview = nullptr;
    uint64_t _PreviewObjectID = 0;
    bool _ClipActive = false;
};
