#include "ColliderComponentEditor.h"
#include "ColliderSceneTool.h"
#include "IPhysicsAuthoring.h"
#include "ui_ColliderComponentEditor.h"
#include <QLineEdit>
#include <QPointer>
#include <QSignalBlocker>

ColliderComponentEditor::ColliderComponentEditor(QWidget* parent,IPhysicsAuthoring* authoring,ColliderSceneTool* tool)
    :ComponentEditor(parent),_Ui(new Ui::ColliderComponentEditorForm()),_Authoring(authoring),_SceneTool(tool) {
    _Ui->setupUi(this);
    QObject::connect(_Ui->fitColliderButton,&QToolButton::clicked,this,[this]{FitCollider();});
    QObject::connect(_Ui->editColliderButton,&QToolButton::clicked,this,[this]{ToggleEditing();});
    if(_SceneTool)_SceneTool->RegisterListener(this,[this]{RefreshTools();});
    for(QLineEdit* field:{_Ui->centerXEdit,_Ui->centerYEdit,_Ui->centerZEdit,_Ui->sizeXEdit,_Ui->sizeYEdit,_Ui->sizeZEdit,_Ui->radiusEdit,_Ui->heightEdit})
        QObject::connect(field,&QLineEdit::editingFinished,this,[this,field]{CommitValues(field);});
    for(QCheckBox* field:{_Ui->enabledCheck,_Ui->triggerCheck,_Ui->convexCheck})QObject::connect(field,&QCheckBox::clicked,this,[this,field]{CommitValues(field);});
    QObject::connect(_Ui->axisCombo,qOverload<int>(&QComboBox::activated),this,[this]{CommitValues(_Ui->axisCombo);});
    _Ui->materialField->Changed=[this](const GameSproutRuntime::ResourceReference&){CommitValues(_Ui->materialField);};
    _Ui->meshField->Changed=[this](const GameSproutRuntime::ResourceReference&){CommitValues(_Ui->meshField);};
    RefreshTools();
}
ColliderComponentEditor::~ColliderComponentEditor(){DetachTools();_Ui->materialField->Changed={};_Ui->meshField->Changed={};delete _Ui;}
void ColliderComponentEditor::DetachTools() {
    if(_SceneTool)_SceneTool->UnregisterListener(this);
    _SceneTool=nullptr;_Authoring=nullptr;RefreshTools();
}
void ColliderComponentEditor::RefreshTools() {
    if(!_Ui)return;
    const bool mesh=_Kind==PhysicsComponentValues::ShapeKind::Mesh;
    const bool target=_Context.Runtime&&_Context.SceneID&&_Context.ObjectID&&_Context.ComponentID;
    const ColliderEditTarget identity{_Context.SceneID,_Context.ObjectID,_Context.ComponentID};
    const QSignalBlocker block(_Ui->editColliderButton);
    _Ui->editColliderButton->setVisible(!mesh);
    _Ui->editColliderButton->setEnabled(_PropertiesEditable&&target&&_SceneTool);
    _Ui->editColliderButton->setChecked(_SceneTool&&_SceneTool->IsTargetActive(identity));
    _Ui->fitColliderButton->setEnabled(_PropertiesEditable&&target&&_Authoring);
}
void ColliderComponentEditor::EditabilityChanged(){RefreshTools();}
void ColliderComponentEditor::FitCollider() {
    if(_Loading||!_PropertiesEditable||!_Authoring)return;
    const ColliderEditTarget target{_Context.SceneID,_Context.ObjectID,_Context.ComponentID};
    QPointer<ColliderComponentEditor> guard(this);
    // End the previous handle session before starting a separate one-step edit.
    if(_SceneTool&&_SceneTool->IsTargetActive(target))_SceneTool->ToggleTarget(target);
    if(!guard)return;
    FinishInput();std::string error;
    const bool fitted=_Authoring->FitCollider(target,error);
    if(!guard)return;
    if(!fitted){Fail(error);return;}
    if(_Context.Report)_Context.Report("");
    if(guard)Reload();
}
void ColliderComponentEditor::ToggleEditing() {
    if(_Loading||!_PropertiesEditable||!_SceneTool)return;
    const ColliderEditTarget target{_Context.SceneID,_Context.ObjectID,_Context.ComponentID};
    QPointer<ColliderComponentEditor> guard(this);
    FinishInput();const bool accepted=_SceneTool->ToggleTarget(target);
    if(!guard)return;
    if(!accepted&&_Context.Report)_Context.Report(_SceneTool->GetSceneToolError());
    if(guard)RefreshTools();
}
void ColliderComponentEditor::ContextChanged() {
    _Ui->materialField->Bind(_Context.Assets,_Context.Library,_Context.Selection,eAssetType::PhysicsMaterial);
    _Ui->materialField->SetContextKey(_Context.Key("Collider","Material"));
    _Ui->meshField->Bind(_Context.Assets,_Context.Library,_Context.Selection,eAssetType::Mesh);
    _Ui->meshField->SetContextKey(_Context.Key("Collider","Mesh"));
    RefreshTools();
}
void ColliderComponentEditor::ReadValues(GameSproutRuntime::IComponent& component) {
    auto* collider=dynamic_cast<GameSproutRuntime::IColliderComponent*>(&component);
    PhysicsComponentValues::Collider data;if(!collider||!PhysicsComponentValues::Read(*collider,data))return;
    _Kind=data.Kind;_Ui->enabledCheck->setChecked(data.Settings.Enabled);_Ui->triggerCheck->setChecked(data.Settings.IsTrigger);
    ShowNumbers({_Ui->centerXEdit,_Ui->centerYEdit,_Ui->centerZEdit},{data.Settings.Center.X,data.Settings.Center.Y,data.Settings.Center.Z});
    const bool box=_Kind==PhysicsComponentValues::ShapeKind::Box,capsule=_Kind==PhysicsComponentValues::ShapeKind::Capsule,mesh=_Kind==PhysicsComponentValues::ShapeKind::Mesh;
    _Ui->sizeHost->setVisible(box);_Ui->label3->setVisible(box);
    _Ui->radiusEdit->setVisible(!box&&!mesh);_Ui->label4->setVisible(!box&&!mesh);
    _Ui->heightEdit->setVisible(capsule);_Ui->label5->setVisible(capsule);
    _Ui->axisCombo->setVisible(capsule);_Ui->label6->setVisible(capsule);
    if(box)ShowNumbers({_Ui->sizeXEdit,_Ui->sizeYEdit,_Ui->sizeZEdit},{data.Box.Size.X,data.Box.Size.Y,data.Box.Size.Z});
    else if(!mesh)ShowNumber(_Ui->radiusEdit,capsule?data.Capsule.Radius:data.Sphere.Radius);
    if(capsule){ShowNumber(_Ui->heightEdit,data.Capsule.Height);_Ui->axisCombo->setCurrentIndex(static_cast<int>(data.Capsule.Axis));}
    _Ui->materialField->SetValue(data.Settings.Material);
    _Ui->convexLabel->setVisible(mesh);_Ui->convexCheck->setVisible(mesh);
    _Ui->meshLabel->setVisible(mesh);_Ui->meshField->setVisible(mesh);_Ui->meshHintLabel->setVisible(mesh);
    _Ui->triggerCheck->setEnabled(!mesh||data.Mesh.Convex);
    if(mesh){_Ui->convexCheck->setChecked(data.Mesh.Convex);_Ui->meshField->SetValue(data.Mesh.Mesh);}
}
void ColliderComponentEditor::CommitValues(QWidget* changed) {
    if(_Loading||!_PropertiesEditable)return;
    auto* collider=dynamic_cast<GameSproutRuntime::IColliderComponent*>(_Context.ResolveComponent());
    PhysicsComponentValues::Collider previous;if(!collider||!PhysicsComponentValues::Read(*collider,previous)){Fail("碰撞器已经删除或场景已切换。");return;}
    auto next=previous;
    if(changed==_Ui->enabledCheck)next.Settings.Enabled=_Ui->enabledCheck->isChecked();
    if(changed==_Ui->triggerCheck)next.Settings.IsTrigger=_Ui->triggerCheck->isChecked();
    if(changed==_Ui->materialField)next.Settings.Material=_Ui->materialField->GetValue();
    if(changed==_Ui->meshField)next.Mesh.Mesh=_Ui->meshField->GetValue();
    if(changed==_Ui->convexCheck){next.Mesh.Convex=_Ui->convexCheck->isChecked();if(!next.Mesh.Convex)next.Settings.IsTrigger=false;}
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
