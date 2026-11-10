#include "ButtonComponentEditor.h"
#include "IUIAuthoring.h"
#include "IObjectEditing.h"
#include "ui_ButtonComponentEditor.h"

ButtonComponentEditor::ButtonComponentEditor(QWidget* parent,IUIAuthoring* authoring)
    :ComponentEditor(parent),_Ui(new Ui::ButtonComponentEditorForm()),_Authoring(authoring) {
    _Ui->setupUi(this);
    QObject::connect(_Ui->interactableCheck,&QCheckBox::clicked,this,[this](bool value) {
        if(_Loading||!_PropertiesEditable||!_Authoring)return;
        std::string error;
        if(!_Authoring->SetButtonInteractable({_Context.SceneID,_Context.ObjectID,_Context.ComponentID},value,error)) {
            Fail(error);
            return;
        }
        if(_Context.Report)_Context.Report("");
        Reload();
    });
}

ButtonComponentEditor::~ButtonComponentEditor(){delete _Ui;}

void ButtonComponentEditor::ReadValues(GameSproutRuntime::IComponent& component) {
    const auto& button=static_cast<const GameSproutRuntime::IButtonComponent&>(component);
    _Ui->interactableCheck->setChecked(button.GetInteractable());
}
