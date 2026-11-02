#include "ScriptPropertyRow.h"
#include "SceneObjectField.h"
#include "ui_ScriptPropertyRow.h"
#include <QLocale>
#include <cmath>
#include <limits>
namespace {
eAssetType AssetType(GameSproutRuntime::ScriptResourceKind type) {
    switch(type){
    case GameSproutRuntime::ScriptResourceKind::Texture:return eAssetType::Texture;
    case GameSproutRuntime::ScriptResourceKind::Mesh:return eAssetType::Mesh;
    case GameSproutRuntime::ScriptResourceKind::Material:return eAssetType::Material;
    case GameSproutRuntime::ScriptResourceKind::AnimationClip:return eAssetType::AnimationClip;
    case GameSproutRuntime::ScriptResourceKind::AnimatorController:return eAssetType::AnimatorController;
    case GameSproutRuntime::ScriptResourceKind::Skeleton:return eAssetType::Skeleton;
    case GameSproutRuntime::ScriptResourceKind::Avatar:return eAssetType::Avatar;
    case GameSproutRuntime::ScriptResourceKind::Audio:return eAssetType::Audio;
    default:return eAssetType::Unknown;
    }
}
}
ScriptPropertyRow::ScriptPropertyRow(const ScriptFieldSnapshot& snapshot,const ScriptFieldTarget& target,const ComponentEditContext& context,QWidget* parent)
    :QWidget(parent),_Ui(new Ui::ScriptPropertyRowForm),_Field(snapshot.Info){
    const auto& field=snapshot.Info;
    const auto& value=snapshot.Value;
    _Ui->setupUi(this);
    QString displayName=QString::fromStdString(field.Name.empty()?field.ID:field.Name);
    if(displayName.startsWith("m_"))displayName.remove(0,2);
    while(displayName.startsWith('_'))displayName.remove(0,1);
    _Ui->nameLabel->setText(displayName.isEmpty()?QString::fromStdString(field.Name):displayName);
    _Ui->nameLabel->setToolTip(QString::fromStdString(field.ID));
    using GameSproutRuntime::ScriptValueKind;
    switch(value.Kind){
    case ScriptValueKind::Boolean:_Ui->valueStack->setCurrentWidget(_Ui->booleanPage);_Ui->booleanCheck->setChecked(value.Boolean);break;
    case ScriptValueKind::Vector3:_Ui->valueStack->setCurrentWidget(_Ui->vectorPage);_Ui->xEdit->setText(QString::number(value.Vector.X,'g',9));_Ui->yEdit->setText(QString::number(value.Vector.Y,'g',9));_Ui->zEdit->setText(QString::number(value.Vector.Z,'g',9));break;
    case ScriptValueKind::Resource:
        _Ui->valueStack->setCurrentWidget(_Ui->resourcePage);_Ui->resourceField->Bind(context.Assets,context.Library,context.Selection,AssetType(field.ResourceKind));
        _Ui->resourceField->SetContextKey(context.Key("Script",field.ID));_Ui->resourceField->SetValue(value.Resource);break;
    case ScriptValueKind::SceneObject: {
        _Ui->valueStack->setCurrentWidget(_Ui->objectPage);
        auto* reference=new SceneObjectField(snapshot,target,context.Scripts,_Ui->objectPage);
        _Ui->objectLayout->addWidget(reference);
        reference->Changed=[this](const auto& data){const auto changed=Changed;if(changed)changed(data);};
        reference->Invalid=[this](const std::string& error){if(Invalid)Invalid(error);};
        break;
    }
    default:
        _Ui->valueStack->setCurrentWidget(_Ui->scalarPage);
        _Ui->scalarEdit->setText(value.Kind==ScriptValueKind::String?QString::fromStdString(value.String):value.Kind==ScriptValueKind::Integer?QString::number(value.Integer):QString::number(value.Number,'g',17));break;
    }
    QObject::connect(_Ui->booleanCheck,&QCheckBox::clicked,this,[this]{Commit();});
    for(auto* edit:{_Ui->scalarEdit,_Ui->xEdit,_Ui->yEdit,_Ui->zEdit})QObject::connect(edit,&QLineEdit::editingFinished,this,[this]{Commit();});
    _Ui->resourceField->Changed=[this](const auto& reference){auto value=_Field.Default;value.Resource=reference;const auto changed=Changed;if(changed)changed(value);};
}
ScriptPropertyRow::~ScriptPropertyRow(){Changed={};Invalid={};_Ui->resourceField->Changed={};delete _Ui;}
void ScriptPropertyRow::Commit(){
    auto value=_Field.Default;bool ok=true;
    using GameSproutRuntime::ScriptValueKind;
    switch(value.Kind){
    case ScriptValueKind::Boolean:value.Boolean=_Ui->booleanCheck->isChecked();break;
    case ScriptValueKind::String:value.String=_Ui->scalarEdit->text().toStdString();ok=value.String.find('\0')==std::string::npos;break;
    case ScriptValueKind::Integer:value.Integer=_Ui->scalarEdit->text().trimmed().toLongLong(&ok,10);break;
    case ScriptValueKind::Number:value.Number=QLocale::c().toDouble(_Ui->scalarEdit->text().trimmed(),&ok);ok=ok&&std::isfinite(value.Number);break;
    case ScriptValueKind::Vector3:{
        float* values[]={&value.Vector.X,&value.Vector.Y,&value.Vector.Z};int i=0;
        for(auto* edit:{_Ui->xEdit,_Ui->yEdit,_Ui->zEdit}){bool valid=false;const double number=QLocale::c().toDouble(edit->text().trimmed(),&valid);if(!valid||!std::isfinite(number)||std::fabs(number)>std::numeric_limits<float>::max()){ok=false;break;}*values[i++]=float(number);}break;
    }
    default:return;
    }
    if(!ok){if(Invalid)Invalid("字段值无效，请输入与字段类型一致的有限数值。");return;}
    const auto changed=Changed;if(changed)changed(value);
}
