#pragma once
#include "ComponentEditor.h"

class IUIAuthoring;
namespace Ui {class ButtonComponentEditorForm;}

class ButtonComponentEditor final : public ComponentEditor {
public:
    ButtonComponentEditor(QWidget* parent,IUIAuthoring* authoring);
    ~ButtonComponentEditor() override;
private:
    void ReadValues(GameSproutRuntime::IComponent&) override;
    Ui::ButtonComponentEditorForm* _Ui=nullptr;
    IUIAuthoring* _Authoring=nullptr;
};
