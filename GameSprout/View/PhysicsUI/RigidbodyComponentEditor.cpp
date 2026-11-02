#include "RigidbodyComponentEditor.h"
#include "PhysicsComponentValues.h"
#include "ui_RigidbodyComponentEditor.h"
#include <array>

RigidbodyComponentEditor::RigidbodyComponentEditor(QWidget* parent):ComponentEditor(parent),_Ui(new Ui::RigidbodyComponentEditorForm()) {
    _Ui->setupUi(this);
    for(QLineEdit* field:{_Ui->massEdit,_Ui->gravityEdit,_Ui->linearDampingEdit,_Ui->angularDampingEdit})
        QObject::connect(field,&QLineEdit::editingFinished,this,[this,field]{CommitValues(field);});
    for(QCheckBox* field:{_Ui->enabledCheck,_Ui->continuousCheck,_Ui->positionXCheck,_Ui->positionYCheck,_Ui->positionZCheck,_Ui->rotationXCheck,_Ui->rotationYCheck,_Ui->rotationZCheck})
        QObject::connect(field,&QCheckBox::clicked,this,[this,field]{CommitValues(field);});
    QObject::connect(_Ui->motionCombo,qOverload<int>(&QComboBox::activated),this,[this]{CommitValues(_Ui->motionCombo);});
    _Ui->gravityEdit->setToolTip(tr("1 使用世界重力；0 关闭重力。"));
    _Ui->continuousCheck->setToolTip(tr("高速物体使用连续碰撞检测，减少穿透。"));
}
RigidbodyComponentEditor::~RigidbodyComponentEditor(){delete _Ui;}
void RigidbodyComponentEditor::ReadValues(GameSproutRuntime::IComponent& component) {
    auto* rigidbody=dynamic_cast<GameSproutRuntime::IRigidbodyComponent*>(&component);if(!rigidbody)return;
    const auto& value=rigidbody->GetSettings();
    _Ui->enabledCheck->setChecked(value.Enabled);_Ui->motionCombo->setCurrentIndex(static_cast<int>(value.MotionType));
    ShowNumbers({_Ui->massEdit,_Ui->gravityEdit,_Ui->linearDampingEdit,_Ui->angularDampingEdit},
        {value.Mass,value.GravityFactor,value.LinearDamping,value.AngularDamping});
    _Ui->continuousCheck->setChecked(value.Continuous);
    const std::array<QCheckBox*,6> constraints{_Ui->positionXCheck,_Ui->positionYCheck,_Ui->positionZCheck,_Ui->rotationXCheck,_Ui->rotationYCheck,_Ui->rotationZCheck};
    for(std::size_t index=0;index<constraints.size();++index)constraints[index]->setChecked((value.Constraints&(1u<<index))!=0);
    const bool dynamic=value.MotionType==GameSproutRuntime::PhysicsMotionType::Dynamic;
    for(QLineEdit* field:{_Ui->massEdit,_Ui->gravityEdit,_Ui->linearDampingEdit,_Ui->angularDampingEdit})field->setEnabled(dynamic);
}
void RigidbodyComponentEditor::CommitValues(QWidget* changed) {
    if(_Loading||!_PropertiesEditable)return;
    auto* body=dynamic_cast<GameSproutRuntime::IRigidbodyComponent*>(_Context.ResolveComponent());
    if(!body){Fail("刚体已经移除或场景已切换。");return;}
    const auto previous=body->GetSettings();auto next=previous;
    const std::pair<QLineEdit*,float*> numbers[]={{_Ui->massEdit,&next.Mass},{_Ui->gravityEdit,&next.GravityFactor},{_Ui->linearDampingEdit,&next.LinearDamping},{_Ui->angularDampingEdit,&next.AngularDamping}};
    for(const auto& [field,value]:numbers)if(changed==field&&!ReadNumber(field,*value)){Fail("刚体参数须为有限数值。");return;}
    if(changed==_Ui->enabledCheck)next.Enabled=_Ui->enabledCheck->isChecked();
    if(changed==_Ui->motionCombo)next.MotionType=static_cast<GameSproutRuntime::PhysicsMotionType>(_Ui->motionCombo->currentIndex());
    if(changed==_Ui->continuousCheck)next.Continuous=_Ui->continuousCheck->isChecked();
    const std::array<QCheckBox*,6> constraints{_Ui->positionXCheck,_Ui->positionYCheck,_Ui->positionZCheck,_Ui->rotationXCheck,_Ui->rotationYCheck,_Ui->rotationZCheck};
    for(std::size_t index=0;index<constraints.size();++index)if(changed==constraints[index]) {
        if(constraints[index]->isChecked())next.Constraints|=1u<<index;else next.Constraints&=~(1u<<index);
    }
    if(PhysicsComponentValues::Equal(previous,next)){Reload();return;}
    auto* scenes=_Context.Scenes;const auto instanceID=_Context.ComponentID;
    const auto fileID=scenes?scenes->GetComponentFileID(_Context.ObjectID,instanceID):0;
    const auto operation=[scenes,fileID,instanceID](GameSproutRuntime::RigidbodySettings value){return [scenes,fileID,instanceID,value](GameSproutRuntime::IGameObject& object,std::string& error){
        auto* component=dynamic_cast<GameSproutRuntime::IRigidbodyComponent*>(PhysicsComponentValues::Resolve(object,scenes,fileID,instanceID));
        if(component&&component->SetSettings(value))return true;
        error="刚体设置无效：质量须为正，阻尼不能为负，全部数值须有限。";return false;
    };};
    FinishInput();_Context.Execute("修改刚体",operation(next),operation(previous));Reload();
}
