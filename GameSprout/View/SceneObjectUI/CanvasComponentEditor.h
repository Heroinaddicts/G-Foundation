#pragma once
#include "ComponentEditor.h"
namespace Ui { class CanvasComponentEditorForm; }
class CanvasComponentEditor final : public ComponentEditor {
public:
    explicit CanvasComponentEditor(QWidget* parent);
    ~CanvasComponentEditor() override;
private:
    void ReadValues(GameSproutRuntime::IComponent& component) override;
    void CommitValues(QWidget* changed);
    Ui::CanvasComponentEditorForm* _Ui = nullptr;
};
