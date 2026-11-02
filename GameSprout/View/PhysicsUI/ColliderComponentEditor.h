#pragma once
#include "ComponentEditor.h"
#include "PhysicsComponentValues.h"
namespace Ui {class ColliderComponentEditorForm;}
class ColliderComponentEditor final:public ComponentEditor {
public:
    explicit ColliderComponentEditor(QWidget*);
    ~ColliderComponentEditor() override;
protected:
    void ContextChanged() override;
    void ReadValues(GameSproutRuntime::IComponent&) override;
private:
    void CommitValues(QWidget* changed);
    Ui::ColliderComponentEditorForm* _Ui=nullptr;
    PhysicsComponentValues::ShapeKind _Kind=PhysicsComponentValues::ShapeKind::Box;
};
