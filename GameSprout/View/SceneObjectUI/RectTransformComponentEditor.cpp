#include "RectTransformComponentEditor.h"
#include "ui_RectTransformComponentEditor.h"
#include <QLineEdit>

RectTransformComponentEditor::RectTransformComponentEditor(QWidget* parent) : ComponentEditor(parent), _Ui(new Ui::RectTransformComponentEditorForm()) {
    _Ui->setupUi(this);
    for (QLineEdit* edit : {_Ui->rectTransformAnchorMinXEdit,_Ui->rectTransformAnchorMinYEdit,_Ui->rectTransformAnchorMaxXEdit,_Ui->rectTransformAnchorMaxYEdit,_Ui->rectTransformPivotXEdit,_Ui->rectTransformPivotYEdit,_Ui->rectTransformAnchoredPositionXEdit,_Ui->rectTransformAnchoredPositionYEdit,_Ui->rectTransformSizeDeltaXEdit,_Ui->rectTransformSizeDeltaYEdit}) QObject::connect(edit,&QLineEdit::editingFinished,this,[this]{CommitValues();});
}
RectTransformComponentEditor::~RectTransformComponentEditor() { delete _Ui; }
void RectTransformComponentEditor::ReadValues(GameSproutRuntime::IComponent& component) {
    auto& c = static_cast<GameSproutRuntime::IRectTransformComponent&>(component);
    const auto v = InspectorValues::Read(c);
    ShowNumbers({_Ui->rectTransformAnchorMinXEdit,_Ui->rectTransformAnchorMinYEdit,_Ui->rectTransformAnchorMaxXEdit,_Ui->rectTransformAnchorMaxYEdit,_Ui->rectTransformPivotXEdit,_Ui->rectTransformPivotYEdit,_Ui->rectTransformAnchoredPositionXEdit,_Ui->rectTransformAnchoredPositionYEdit,_Ui->rectTransformSizeDeltaXEdit,_Ui->rectTransformSizeDeltaYEdit},{v.Minimum.X,v.Minimum.Y,v.Maximum.X,v.Maximum.Y,v.Pivot.X,v.Pivot.Y,v.Position.X,v.Position.Y,v.Size.X,v.Size.Y});
}
void RectTransformComponentEditor::CommitValues() {
    if (_Loading) return;
    std::vector<float> a;
    if (!ReadNumbers({_Ui->rectTransformAnchorMinXEdit,_Ui->rectTransformAnchorMinYEdit,_Ui->rectTransformAnchorMaxXEdit,_Ui->rectTransformAnchorMaxYEdit,_Ui->rectTransformPivotXEdit,_Ui->rectTransformPivotYEdit,_Ui->rectTransformAnchoredPositionXEdit,_Ui->rectTransformAnchoredPositionYEdit,_Ui->rectTransformSizeDeltaXEdit,_Ui->rectTransformSizeDeltaYEdit},a)) { Fail("请输入有限数字，可使用科学计数法。"); return; }
    InspectorValues::RectTransform v{{a[0],a[1]},{a[2],a[3]},{a[4],a[5]},{a[6],a[7]},{a[8],a[9]}};
    Commit<GameSproutRuntime::IRectTransformComponent>(v,"修改 RectTransform");
}
