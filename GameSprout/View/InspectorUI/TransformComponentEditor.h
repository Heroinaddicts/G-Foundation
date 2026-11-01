#pragma once
#include "ComponentEditor.h"
namespace Ui { class TransformComponentEditorForm; }
class TransformComponentEditor final : public ComponentEditor {
public:
    explicit TransformComponentEditor(QWidget* parent);
    ~TransformComponentEditor() override;
private:
    void ContextChanged() override;
    void ReadValues(GameSproutRuntime::IComponent& component) override;
    void CommitValues(bool rotationEdited);
    void RefreshAnimationState();
    Ui::TransformComponentEditorForm* _Ui = nullptr;
    GameSproutRuntime::Quaternion _DisplayedRotation;
    GameSproutRuntime::Vector3 _DisplayedEuler;
    bool _HasDisplayedRotation=false;
};
