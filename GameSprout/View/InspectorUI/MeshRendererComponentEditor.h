#pragma once
#include "ComponentEditor.h"
namespace Ui { class MeshRendererComponentEditorForm; }
class MaterialSlotEditor;
class MeshRendererComponentEditor final : public ComponentEditor {
public:
    explicit MeshRendererComponentEditor(QWidget* parent);
    ~MeshRendererComponentEditor() override;
private:
    void ReadValues(GameSproutRuntime::IComponent& component) override;
    void ContextChanged() override;
    void Modify(const std::function<void(InspectorValues::MeshRenderer&)>& change,const std::string& label);
    Ui::MeshRendererComponentEditorForm* _Ui = nullptr;
    std::vector<MaterialSlotEditor*> _Slots;
};
