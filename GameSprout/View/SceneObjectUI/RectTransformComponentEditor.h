#pragma once
#include "ComponentEditor.h"
namespace Ui { class RectTransformComponentEditorForm; }
class RectTransformComponentEditor final : public ComponentEditor {
public:
    explicit RectTransformComponentEditor(QWidget* parent);
    ~RectTransformComponentEditor() override;
private:
    void ReadValues(GameSproutRuntime::IComponent& component) override;
    void CommitValues();
    Ui::RectTransformComponentEditorForm* _Ui = nullptr;
};
