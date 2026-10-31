#pragma once
#include "ComponentEditor.h"
namespace Ui { class TransformComponentEditorForm; }
class TransformComponentEditor final : public ComponentEditor {
public:
    explicit TransformComponentEditor(QWidget* parent);
    ~TransformComponentEditor() override;
private:
    void ReadValues(GameSproutRuntime::IComponent& component) override;
    void CommitValues();
    void RefreshAnimationState();
    Ui::TransformComponentEditorForm* _Ui = nullptr;
};
