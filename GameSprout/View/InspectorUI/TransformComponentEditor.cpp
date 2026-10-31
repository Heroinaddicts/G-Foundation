#include "TransformComponentEditor.h"
#include "ui_TransformComponentEditor.h"
#include <QLineEdit>

TransformComponentEditor::TransformComponentEditor(QWidget* parent) : ComponentEditor(parent), _Ui(new Ui::TransformComponentEditorForm()) {
    _Ui->setupUi(this);
    for (QLineEdit* edit : {_Ui->transformPositionXEdit,_Ui->transformPositionYEdit,_Ui->transformPositionZEdit,_Ui->transformRotationXEdit,_Ui->transformRotationYEdit,_Ui->transformRotationZEdit,_Ui->transformRotationWEdit,_Ui->transformScaleXEdit,_Ui->transformScaleYEdit,_Ui->transformScaleZEdit}) QObject::connect(edit,&QLineEdit::editingFinished,this,[this]{CommitValues();});
}
TransformComponentEditor::~TransformComponentEditor() { delete _Ui; }
void TransformComponentEditor::ReadValues(GameSproutRuntime::IComponent& component) {
    auto& c = static_cast<GameSproutRuntime::ITransformComponent&>(component);
    const auto v = InspectorValues::Read(c);
    ShowNumbers({_Ui->transformPositionXEdit,_Ui->transformPositionYEdit,_Ui->transformPositionZEdit,_Ui->transformRotationXEdit,_Ui->transformRotationYEdit,_Ui->transformRotationZEdit,_Ui->transformRotationWEdit,_Ui->transformScaleXEdit,_Ui->transformScaleYEdit,_Ui->transformScaleZEdit},{v.Position.X,v.Position.Y,v.Position.Z,v.Rotation.X,v.Rotation.Y,v.Rotation.Z,v.Rotation.W,v.Scale.X,v.Scale.Y,v.Scale.Z});
    setEnabled(c.GetGameObject()->GetScene() != c.GetGameObject());
}
void TransformComponentEditor::CommitValues() {
    if (_Loading) return;
    std::vector<float> a;
    if (!ReadNumbers({_Ui->transformPositionXEdit,_Ui->transformPositionYEdit,_Ui->transformPositionZEdit,_Ui->transformRotationXEdit,_Ui->transformRotationYEdit,_Ui->transformRotationZEdit,_Ui->transformRotationWEdit,_Ui->transformScaleXEdit,_Ui->transformScaleYEdit,_Ui->transformScaleZEdit},a)) { Fail("请输入有限数字，可使用科学计数法。"); return; }
    InspectorValues::Transform v{{a[0],a[1],a[2]},{a[3],a[4],a[5],a[6]},{a[7],a[8],a[9]}};
    Commit<GameSproutRuntime::ITransformComponent>(v,"修改 Transform");
}
