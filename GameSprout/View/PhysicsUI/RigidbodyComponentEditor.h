#pragma once
#include "ComponentEditor.h"
namespace Ui {class RigidbodyComponentEditorForm;}
class RigidbodyComponentEditor final:public ComponentEditor {
public:
    explicit RigidbodyComponentEditor(QWidget*);
    ~RigidbodyComponentEditor() override;
protected:
    void ReadValues(GameSproutRuntime::IComponent&) override;
private:
    void CommitValues(QWidget* changed);
    Ui::RigidbodyComponentEditorForm* _Ui=nullptr;
};
