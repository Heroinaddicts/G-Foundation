#pragma once
#include "ComponentEditor.h"
#include "PhysicsComponentValues.h"
class IPhysicsAuthoring;
class ColliderSceneTool;
namespace Ui {class ColliderComponentEditorForm;}
class ColliderComponentEditor final:public ComponentEditor {
public:
    explicit ColliderComponentEditor(QWidget*,IPhysicsAuthoring* = nullptr,ColliderSceneTool* = nullptr);
    ~ColliderComponentEditor() override;
    void DetachTools();
protected:
    void ContextChanged() override;
    void ReadValues(GameSproutRuntime::IComponent&) override;
    void EditabilityChanged() override;
private:
    void RefreshTools();
    void FitCollider();
    void ToggleEditing();
    void CommitValues(QWidget* changed);
    Ui::ColliderComponentEditorForm* _Ui=nullptr;
    IPhysicsAuthoring* _Authoring=nullptr;
    ColliderSceneTool* _SceneTool=nullptr;
    PhysicsComponentValues::ShapeKind _Kind=PhysicsComponentValues::ShapeKind::Box;
};
