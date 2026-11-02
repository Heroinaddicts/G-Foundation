#include "PhysicsMaterialPanel.h"
#include "IPhysicsAuthoring.h"
#include "IAssetsManager.h"
#include "InspectorNumberText.h"
#include "ui_PhysicsMaterialPanel.h"
#include <QFileInfo>
#include <QLocale>
#include <cmath>
#include <limits>

namespace {
bool Equal(const GameSproutRuntime::PhysicsMaterialData& a,const GameSproutRuntime::PhysicsMaterialData& b) {
    return a.Friction==b.Friction&&a.Restitution==b.Restitution&&a.FrictionCombine==b.FrictionCombine&&a.RestitutionCombine==b.RestitutionCombine;
}
bool Number(const QLineEdit* field,float& result) {
    bool valid=false;const double value=QLocale::c().toDouble(field->text().trimmed(),&valid);
    if(!valid||!std::isfinite(value)||std::fabs(value)>std::numeric_limits<float>::max())return false;
    result=static_cast<float>(value);return true;
}
}
PhysicsMaterialPanel::PhysicsMaterialPanel(const InspectorServices& services,IPhysicsAuthoring* authoring,QWidget* parent)
    :InspectorPanel(services,parent),_Ui(new Ui::PhysicsMaterialPanelForm()),_Authoring(authoring) {
    _Ui->setupUi(this);
    for(QLineEdit* field:{_Ui->frictionEdit,_Ui->restitutionEdit})QObject::connect(field,&QLineEdit::textEdited,this,[this]{if(!_Loading&&Changed)Changed();});
    for(QComboBox* field:{_Ui->frictionCombineCombo,_Ui->restitutionCombineCombo})QObject::connect(field,qOverload<int>(&QComboBox::activated),this,[this]{if(!_Loading&&Changed)Changed();});
    _Ui->frictionEdit->setToolTip(tr("非负表面摩擦系数；数值越大，接触时越难滑动。"));
    _Ui->restitutionEdit->setToolTip(tr("0 不反弹，1 为完全弹性碰撞；实际效果还受求解器及低速阈值影响。"));
}
PhysicsMaterialPanel::~PhysicsMaterialPanel(){delete _Ui;}
void PhysicsMaterialPanel::Display(const GameSproutRuntime::PhysicsMaterialData& value) {
    _Loading=true;
    _Ui->frictionEdit->setText(InspectorNumbers::Format(value.Friction));_Ui->restitutionEdit->setText(InspectorNumbers::Format(value.Restitution));
    _Ui->frictionEdit->setCursorPosition(0);_Ui->restitutionEdit->setCursorPosition(0);
    _Ui->frictionCombineCombo->setCurrentIndex(static_cast<int>(value.FrictionCombine));
    _Ui->restitutionCombineCombo->setCurrentIndex(static_cast<int>(value.RestitutionCombine));_Loading=false;
}
bool PhysicsMaterialPanel::Load(const InspectorTarget& target,bool changed,std::string& error) {
    error.clear();
    if(!_Authoring||!_Services.Assets||target.Scene.ObjectID||target.Assets.size()!=1){error="请选择一个物理材质资源。";return false;}
    const auto& selection=target.Assets.front();auto* meta=_Services.Assets->QueryAsset(Guid(selection.GuidString));
    if(!meta||meta->_ImporterType!=eAssetImporterType::PhysicsMaterial){error="物理材质已经删除。";return false;}
    const GameSproutRuntime::ResourceReference reference{selection.GuidString,selection.IsSubAsset?selection.LocalID:0};
    GameSproutRuntime::PhysicsMaterialData current;std::string snapshot;
    if(!_Authoring->LoadPhysicsMaterialSource(reference,current,snapshot,error))return false;
    if(changed||!_Loaded||target.Assets!=_Target.Assets||!HasPendingChanges()) {
        _Baseline=current;_SourceSnapshot=snapshot;Display(current);
    }else if(snapshot!=_SourceSnapshot)error="物理材质源已被外部修改；当前草稿已保留，请恢复后重新编辑。";
    _Target=target;_Reference=reference;_Loaded=true;_Presentation={};
    _Presentation.Name=QFileInfo(QString::fromStdString(meta->GetPath())).completeBaseName();_Presentation.Type=QStringLiteral("Physics Material");
    _Presentation.Path=QStringLiteral("Assets/")+QString::fromStdString(meta->GetPath());_Presentation.Guid=QString::fromStdString(selection.GuidString);
    _Presentation.LocalID=selection.IsSubAsset?QString::number(selection.LocalID):QStringLiteral("源文件");
    _Presentation.State=meta->GetImportState()==eAssetImportState::Failed?QStringLiteral("导入失败"):meta->GetImportState()==eAssetImportState::Pending?QStringLiteral("正在导入"):QStringLiteral("就绪");
    _Presentation.Editable=true;_Presentation.ApplyText=QStringLiteral("保存物理材质");return true;
}
bool PhysicsMaterialPanel::Read(GameSproutRuntime::PhysicsMaterialData& data,std::string& error) const {
    if(!Number(_Ui->frictionEdit,data.Friction)||data.Friction<0){error="摩擦系数必须是非负有限数。";return false;}
    if(!Number(_Ui->restitutionEdit,data.Restitution)||data.Restitution<0||data.Restitution>1){error="恢复系数须在 0..1 范围。";return false;}
    const int friction=_Ui->frictionCombineCombo->currentIndex(),restitution=_Ui->restitutionCombineCombo->currentIndex();
    if(friction<0||friction>3||restitution<0||restitution>3){error="请选择有效的合并模式。";return false;}
    data.FrictionCombine=static_cast<GameSproutRuntime::PhysicsMaterialCombine>(friction);
    data.RestitutionCombine=static_cast<GameSproutRuntime::PhysicsMaterialCombine>(restitution);error.clear();return true;
}
bool PhysicsMaterialPanel::HasPendingChanges() const {
    if(!_Loaded)return false;GameSproutRuntime::PhysicsMaterialData draft;std::string error;return !Read(draft,error)||!Equal(draft,_Baseline);
}
bool PhysicsMaterialPanel::Validate(std::string& error) const {
    if(!_Loaded){error="未加载物理材质。";return false;}GameSproutRuntime::PhysicsMaterialData draft;return Read(draft,error);
}
bool PhysicsMaterialPanel::Apply(std::string& error) {
    GameSproutRuntime::PhysicsMaterialData draft;if(!_Loaded||!_Authoring||!Read(draft,error))return false;
    if(!_Authoring->SavePhysicsMaterialSource(_Reference,draft,_SourceSnapshot,error))return false;
    return Revert(error);
}
bool PhysicsMaterialPanel::Revert(std::string& error) {if(!_Loaded){error="未加载物理材质。";return false;}const auto target=_Target;return Load(target,true,error);}
void PhysicsMaterialPanel::Deactivate(){_Loaded=false;_Target={};_Reference={};_Presentation={};_SourceSnapshot.clear();}
