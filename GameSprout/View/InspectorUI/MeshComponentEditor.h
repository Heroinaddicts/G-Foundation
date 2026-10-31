#pragma once
#include "ComponentEditor.h"
namespace Ui { class MeshComponentEditorForm; }
class ResourceField;
class MeshComponentEditor final : public ComponentEditor {
public:
    explicit MeshComponentEditor(QWidget* parent);
    ~MeshComponentEditor() override;
private:
    void ReadValues(GameSproutRuntime::IComponent& component) override;
    void ContextChanged() override;
    Ui::MeshComponentEditorForm* _Ui = nullptr;
    ResourceField* _Mesh = nullptr;
};
