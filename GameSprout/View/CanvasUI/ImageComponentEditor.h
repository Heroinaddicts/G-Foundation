#pragma once
#include "ComponentEditor.h"
namespace Ui { class ImageComponentEditorForm; }
class ImageComponentEditor final : public ComponentEditor {
public:
    explicit ImageComponentEditor(QWidget* parent);
    ~ImageComponentEditor() override;
private:
    void ReadValues(GameSproutRuntime::IComponent& component) override;
    void CommitValues(QWidget* changed);
    void ContextChanged() override;
    Ui::ImageComponentEditorForm* _Ui = nullptr;
};
