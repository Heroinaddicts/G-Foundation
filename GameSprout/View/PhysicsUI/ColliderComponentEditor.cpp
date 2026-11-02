#include "ColliderComponentEditor.h"
#include "ui_ColliderComponentEditor.h"
#include <QLineEdit>

ColliderComponentEditor::ColliderComponentEditor(QWidget* parent):ComponentEditor(parent),_Ui(new Ui::ColliderComponentEditorForm()) {
    _Ui->setupUi(this);
    for(QLineEdit* field:{_Ui->centerXEdit,_Ui->centerYEdit,_Ui->centerZEdit,_Ui->sizeXEdit,_Ui->sizeYEdit,_Ui->sizeZEdit,_Ui->radiusEdit,_Ui->heightEdit})
        QObject::connect(field,&QLineEdit::editingFinished,this,[this]{CommitValues();});
    for(QCheckBox* field:{_Ui->enabledCheck,_Ui->triggerCheck})QObject::connect(field,&QCheckBox::clicked,this,[this]{CommitValues();});
    QObject::connect(_Ui->axisCombo,qOverload<int>(&QComboBox::activated),this,[this]{CommitValues();});
    _Ui->materialField->Changed=[this](const GameSproutRuntime::ResourceReference&){CommitValues();};
}
ColliderComponentEditor::~ColliderComponentEditor(){_Ui->materialField->Changed={};delete _Ui;}
void ColliderComponentEditor::ContextChanged() {
    _Ui->materialField->Bind(_Context.Assets,_Context.Library,_Context.Selection,eAssetType::PhysicsMaterial);
    _Ui->materialField->SetContextKey(_Context.Key("Collider","Material"));
}
void ColliderComponentEditor::ReadValues(GameSproutRuntime::IComponent& component) {
    auto* collider=dynamic_cast<GameSproutRuntime::IColliderComponent*>(&component);
    PhysicsComponentValues::Collider data;if(!collider||!PhysicsComponentValues::Read(*collider,data))return;
    _Kind=data.Kind;_Ui->enabledCheck->setChecked(data.Settings.Enabled);_Ui->triggerCheck->setChecked(data.Settings.IsTrigger);
    ShowNumbers({_Ui->centerXEdit,_Ui->centerYEdit,_Ui->centerZEdit},{data.Settings.Center.X,data.Settings.Center.Y,data.Settings.Center.Z});
    const bool box=_Kind==PhysicsComponentValues::ShapeKind::Box,capsule=_Kind==PhysicsComponentValues::ShapeKind::Capsule;
    _Ui->sizeHost->setVisible(box);_Ui->label3->setVisible(box);
    _Ui->radiusEdit->setVisible(!box);_Ui->label4->setVisible(!box);
    _Ui->heightEdit->setVisible(capsule);_Ui->label5->setVisible(capsule);
    _Ui->axisCombo->setVisible(capsule);_Ui->label6->setVisible(capsule);
    if(box)ShowNumbers({_Ui->sizeXEdit,_Ui->sizeYEdit,_Ui->sizeZEdit},{data.Box.Size.X,data.Box.Size.Y,data.Box.Size.Z});
    else ShowNumber(_Ui->radiusEdit,capsule?data.Capsule.Radius:data.Sphere.Radius);
    if(capsule){ShowNumber(_Ui->heightEdit,data.Capsule.Height);_Ui->axisCombo->setCurrentIndex(static_cast<int>(data.Capsule.Axis));}
    _Ui->materialField->SetValue(data.Settings.Material);
}
void ColliderComponentEditor::CommitValues() {
    if(_Loading)return;
    auto* collider=dynamic_cast<GameSproutRuntime::IColliderComponent*>(_Context.ResolveComponent());
    PhysicsComponentValues::Collider previous;if(!collider||!PhysicsComponentValues::Read(*collider,previous)){Fail("碰撞器已经删除或场景已切换。");return;}
    auto next=previous;std::vector<float> center;
    if(!ReadNumbers({_Ui->centerXEdit,_Ui->centerYEdit,_Ui->centerZEdit},center)){Fail("中心坐标必须为有限数值。");return;}
    next.Settings.Center={center[0],center[1],center[2]};next.Settings.Enabled=_Ui->enabledCheck->isChecked();next.Settings.IsTrigger=_Ui->triggerCheck->isChecked();
    next.Settings.Material=_Ui->materialField->GetValue();
    if(_Kind==PhysicsComponentValues::ShapeKind::Box){std::vector<float> size;if(!ReadNumbers({_Ui->sizeXEdit,_Ui->sizeYEdit,_Ui->sizeZEdit},size)){Fail("尺寸必须为有限数值。");return;}next.Box.Size={size[0],size[1],size[2]};}
    else if(_Kind==PhysicsComponentValues::ShapeKind::Sphere){if(!ReadNumber(_Ui->radiusEdit,next.Sphere.Radius)){Fail("半径必须为有限数值。");return;}}
    else if(!ReadNumber(_Ui->radiusEdit,next.Capsule.Radius)||!ReadNumber(_Ui->heightEdit,next.Capsule.Height)){Fail("半径和高度必须为有限数值。");return;}
    else next.Capsule.Axis=static_cast<GameSproutRuntime::PhysicsAxis>(_Ui->axisCombo->currentIndex());
    if(PhysicsComponentValues::Equal(previous,next)){Reload();return;}
    auto* scenes=_Context.Scenes;const auto instanceID=_Context.ComponentID;
    const auto fileID=scenes?scenes->GetComponentFileID(_Context.ObjectID,instanceID):0;
    const auto operation=[scenes,fileID,instanceID](PhysicsComponentValues::Collider value){return [scenes,fileID,instanceID,value](GameSproutRuntime::IGameObject& object,std::string& error){
        auto* component=dynamic_cast<GameSproutRuntime::IColliderComponent*>(PhysicsComponentValues::Resolve(object,scenes,fileID,instanceID));
        if(!component){error="碰撞器已经移除。";return false;}return PhysicsComponentValues::Write(*component,value,error);
    };};
    _Context.Execute("修改碰撞器",operation(next),operation(previous));Reload();
}
