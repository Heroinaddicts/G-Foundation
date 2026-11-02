#include "RectTransformComponentEditor.h"
#include "ui_RectTransformComponentEditor.h"
#include <QLineEdit>

RectTransformComponentEditor::RectTransformComponentEditor(QWidget* parent) : ComponentEditor(parent), _Ui(new Ui::RectTransformComponentEditorForm()) {
    _Ui->setupUi(this);
    for (QLineEdit* edit : {_Ui->rectTransformAnchorMinXEdit,_Ui->rectTransformAnchorMinYEdit,_Ui->rectTransformAnchorMaxXEdit,_Ui->rectTransformAnchorMaxYEdit,_Ui->rectTransformPivotXEdit,_Ui->rectTransformPivotYEdit,_Ui->rectTransformAnchoredPositionXEdit,_Ui->rectTransformAnchoredPositionYEdit,_Ui->rectTransformSizeDeltaXEdit,_Ui->rectTransformSizeDeltaYEdit}) QObject::connect(edit,&QLineEdit::editingFinished,this,[this,edit]{CommitValues(edit);});
}
RectTransformComponentEditor::~RectTransformComponentEditor() { delete _Ui; }
void RectTransformComponentEditor::ReadValues(GameSproutRuntime::IComponent& component) {
    auto& c = static_cast<GameSproutRuntime::IRectTransformComponent&>(component);
    const auto v = InspectorValues::Read(c);
    ShowNumbers({_Ui->rectTransformAnchorMinXEdit,_Ui->rectTransformAnchorMinYEdit,_Ui->rectTransformAnchorMaxXEdit,_Ui->rectTransformAnchorMaxYEdit,_Ui->rectTransformPivotXEdit,_Ui->rectTransformPivotYEdit,_Ui->rectTransformAnchoredPositionXEdit,_Ui->rectTransformAnchoredPositionYEdit,_Ui->rectTransformSizeDeltaXEdit,_Ui->rectTransformSizeDeltaYEdit},{v.Minimum.X,v.Minimum.Y,v.Maximum.X,v.Maximum.Y,v.Pivot.X,v.Pivot.Y,v.Position.X,v.Position.Y,v.Size.X,v.Size.Y});
}
void RectTransformComponentEditor::CommitValues(QWidget* changed) {
    if (_Loading||!_PropertiesEditable) return;
    auto* component=dynamic_cast<GameSproutRuntime::IRectTransformComponent*>(_Context.ResolveComponent());if(!component){Fail("RectTransform 已失效。");return;}
    auto v=InspectorValues::Read(*component);
    const std::pair<QLineEdit*,float*> fields[]={{_Ui->rectTransformAnchorMinXEdit,&v.Minimum.X},{_Ui->rectTransformAnchorMinYEdit,&v.Minimum.Y},{_Ui->rectTransformAnchorMaxXEdit,&v.Maximum.X},{_Ui->rectTransformAnchorMaxYEdit,&v.Maximum.Y},
        {_Ui->rectTransformPivotXEdit,&v.Pivot.X},{_Ui->rectTransformPivotYEdit,&v.Pivot.Y},{_Ui->rectTransformAnchoredPositionXEdit,&v.Position.X},{_Ui->rectTransformAnchoredPositionYEdit,&v.Position.Y},{_Ui->rectTransformSizeDeltaXEdit,&v.Size.X},{_Ui->rectTransformSizeDeltaYEdit,&v.Size.Y}};
    for(const auto& [field,value]:fields)if(changed==field&&!ReadNumber(field,*value)){Fail("请输入有限数字，可使用科学计数法。");return;}
    Commit<GameSproutRuntime::IRectTransformComponent>(v,"修改 RectTransform");
}
