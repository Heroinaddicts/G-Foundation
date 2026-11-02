#include "ColliderComponentEditor.h"
#include "ui_ColliderComponentEditor.h"
#include <QLineEdit>

ColliderComponentEditor::ColliderComponentEditor(QWidget* parent):ComponentEditor(parent),_Ui(new Ui::ColliderComponentEditorForm()) {
    _Ui->setupUi(this);
    for(QLineEdit* field:{_Ui->centerXEdit,_Ui->centerYEdit,_Ui->centerZEdit,_Ui->sizeXEdit,_Ui->sizeYEdit,_Ui->sizeZEdit,_Ui->radiusEdit,_Ui->heightEdit})
        QObject::connect(field,&QLineEdit::editingFinished,this,[this,field]{CommitValues(field);});
    for(QCheckBox* field:{_Ui->enabledCheck,_Ui->triggerCheck})QObject::connect(field,&QCheckBox::clicked,this,[this,field]{CommitValues(field);});
    QObject::connect(_Ui->axisCombo,qOverload<int>(&QComboBox::activated),this,[this]{CommitValues(_Ui->axisCombo);});
    _Ui->materialField->Changed=[this](const GameSproutRuntime::ResourceReference&){CommitValues(_Ui->materialField);};
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
void ColliderComponentEditor::CommitValues(QWidget* changed) {
    if(_Loading||!_PropertiesEditable)return;
    auto* collider=dynamic_cast<GameSproutRuntime::IColliderComponent*>(_Context.ResolveComponent());
    PhysicsComponentValues::Collider previous;if(!collider||!PhysicsComponentValues::Read(*collider,previous)){Fail("碰撞器已经删除或场景已切换。");return;}
    auto next=previous;
    if(changed==_Ui->enabledCheck)next.Settings.Enabled=_Ui->enabledCheck->isChecked();
    if(changed==_Ui->triggerCheck)next.Settings.IsTrigger=_Ui->triggerCheck->isChecked();
    if(changed==_Ui->materialField)next.Settings.Material=_Ui->materialField->GetValue();
    if(changed==_Ui->axisCombo)next.Capsule.Axis=static_cast<GameSproutRuntime::PhysicsAxis>(_Ui->axisCombo->currentIndex());
    float* radius=_Kind==PhysicsComponentValues::ShapeKind::Sphere?&next.Sphere.Radius:&next.Capsule.Radius;
    const std::pair<QLineEdit*,float*> numbers[]={{_Ui->centerXEdit,&next.Settings.Center.X},{_Ui->centerYEdit,&next.Settings.Center.Y},{_Ui->centerZEdit,&next.Settings.Center.Z},
        {_Ui->sizeXEdit,&next.Box.Size.X},{_Ui->sizeYEdit,&next.Box.Size.Y},{_Ui->sizeZEdit,&next.Box.Size.Z},{_Ui->radiusEdit,radius},{_Ui->heightEdit,&next.Capsule.Height}};
    for(const auto& [field,value]:numbers)if(changed==field&&!ReadNumber(field,*value)){Fail("碰撞器参数须为有限数值。");return;}
    if(PhysicsComponentValues::Equal(previous,next)){Reload();return;}
    auto* scenes=_Context.Scenes;const auto instanceID=_Context.ComponentID;
    const auto fileID=scenes?scenes->GetComponentFileID(_Context.ObjectID,instanceID):0;
    const auto operation=[scenes,fileID,instanceID](PhysicsComponentValues::Collider value){return [scenes,fileID,instanceID,value](GameSproutRuntime::IGameObject& object,std::string& error){
        auto* component=dynamic_cast<GameSproutRuntime::IColliderComponent*>(PhysicsComponentValues::Resolve(object,scenes,fileID,instanceID));
        if(!component){error="碰撞器已经移除。";return false;}return PhysicsComponentValues::Write(*component,value,error);
    };};
    FinishInput();_Context.Execute("修改碰撞器",operation(next),operation(previous));Reload();
}
