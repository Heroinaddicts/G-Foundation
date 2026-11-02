#include "CanvasComponentEditor.h"
#include "ui_CanvasComponentEditor.h"
#include <QLineEdit>

CanvasComponentEditor::CanvasComponentEditor(QWidget* parent) : ComponentEditor(parent), _Ui(new Ui::CanvasComponentEditorForm()) {
    _Ui->setupUi(this);
    for (QLineEdit* edit : {_Ui->canvasResolutionXEdit,_Ui->canvasResolutionYEdit,_Ui->canvasPixelsPerUnitEdit}) QObject::connect(edit,&QLineEdit::editingFinished,this,[this,edit]{CommitValues(edit);});
    QObject::connect(_Ui->canvasModeCombo,qOverload<int>(&QComboBox::activated),this,[this]{CommitValues(_Ui->canvasModeCombo);});
}
CanvasComponentEditor::~CanvasComponentEditor() { delete _Ui; }
void CanvasComponentEditor::ReadValues(GameSproutRuntime::IComponent& component) {
    auto& c = static_cast<GameSproutRuntime::ICanvasComponent&>(component);
    const auto v = InspectorValues::Read(c);
    ShowNumbers({_Ui->canvasResolutionXEdit,_Ui->canvasResolutionYEdit,_Ui->canvasPixelsPerUnitEdit},{v.Resolution.X,v.Resolution.Y,v.Pixels});
    _Ui->canvasModeCombo->setCurrentIndex(static_cast<int>(v.Mode));
}
void CanvasComponentEditor::CommitValues(QWidget* changed) {
    if (_Loading||!_PropertiesEditable) return;
    auto* component=dynamic_cast<GameSproutRuntime::ICanvasComponent*>(_Context.ResolveComponent());if(!component){Fail("Canvas 已失效。");return;}
    auto v=InspectorValues::Read(*component);
    if(changed==_Ui->canvasModeCombo)v.Mode=static_cast<GameSproutRuntime::CanvasRenderMode>(_Ui->canvasModeCombo->currentIndex());
    const std::pair<QLineEdit*,float*> fields[]={{_Ui->canvasResolutionXEdit,&v.Resolution.X},{_Ui->canvasResolutionYEdit,&v.Resolution.Y},{_Ui->canvasPixelsPerUnitEdit,&v.Pixels}};
    for(const auto& [field,value]:fields)if(changed==field&&!ReadNumber(field,*value)){Fail("请输入有限数字，可使用科学计数法。");return;}
    Commit<GameSproutRuntime::ICanvasComponent>(v,"修改 Canvas");
}
