#pragma once
#include "ComponentEditContext.h"
#include <QWidget>
namespace Ui { class MaterialSlotEditorForm; }
class ResourceField;
class MaterialSlotEditor final : public QWidget {
public:
    explicit MaterialSlotEditor(QWidget* parent);
    ~MaterialSlotEditor() override;
    void Load(const ComponentEditContext& context, std::size_t slot, std::size_t count, const GameSproutRuntime::ResourceReference& resource);
    void Deactivate();
    std::function<void(std::size_t,const GameSproutRuntime::ResourceReference&)> Changed;
    std::function<void(std::size_t,int)> Move;
    std::function<void(std::size_t)> Remove;
private:
    Ui::MaterialSlotEditorForm* _Ui = nullptr;
    ResourceField* _Resource = nullptr;
    std::size_t _Slot = 0;
};
