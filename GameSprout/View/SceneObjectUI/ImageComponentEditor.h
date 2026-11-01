#pragma once
#include "ComponentEditor.h"
class ResourceField;
namespace Ui { class ImageComponentEditorForm; }
class ImageComponentEditor final : public ComponentEditor {
public:
    explicit ImageComponentEditor(QWidget* parent);
    ~ImageComponentEditor() override;
private:
    void ReadValues(GameSproutRuntime::IComponent& component) override;
    void CommitValues();
    void ContextChanged() override;
    ResourceField* _Texture = nullptr;
    Ui::ImageComponentEditorForm* _Ui = nullptr;
};
