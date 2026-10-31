#include "CanvasComponentEditor.h"
#include "ui_CanvasComponentEditor.h"
#include <QLineEdit>

CanvasComponentEditor::CanvasComponentEditor(QWidget* parent) : ComponentEditor(parent), _Ui(new Ui::CanvasComponentEditorForm()) {
    _Ui->setupUi(this);
    for (QLineEdit* edit : {_Ui->canvasResolutionXEdit,_Ui->canvasResolutionYEdit,_Ui->canvasPixelsPerUnitEdit}) QObject::connect(edit,&QLineEdit::editingFinished,this,[this]{CommitValues();});
    QObject::connect(_Ui->canvasModeCombo,qOverload<int>(&QComboBox::activated),this,[this]{CommitValues();});
}
CanvasComponentEditor::~CanvasComponentEditor() { delete _Ui; }
void CanvasComponentEditor::ReadValues(GameSproutRuntime::IComponent& component) {
    auto& c = static_cast<GameSproutRuntime::ICanvasComponent&>(component);
    const auto v = InspectorValues::Read(c);
    ShowNumbers({_Ui->canvasResolutionXEdit,_Ui->canvasResolutionYEdit,_Ui->canvasPixelsPerUnitEdit},{v.Resolution.X,v.Resolution.Y,v.Pixels});
    _Ui->canvasModeCombo->setCurrentIndex(static_cast<int>(v.Mode));
}
void CanvasComponentEditor::CommitValues() {
    if (_Loading) return;
    std::vector<float> a;
    if (!ReadNumbers({_Ui->canvasResolutionXEdit,_Ui->canvasResolutionYEdit,_Ui->canvasPixelsPerUnitEdit},a)) { Fail("请输入有限数字，可使用科学计数法。"); return; }
    InspectorValues::Canvas v{static_cast<GameSproutRuntime::CanvasRenderMode>(_Ui->canvasModeCombo->currentIndex()),{a[0],a[1]},a[2]};
    Commit<GameSproutRuntime::ICanvasComponent>(v,"修改 Canvas");
}
