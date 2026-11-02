#include "ScriptPropertyRow.h"
#include "SceneObjectField.h"
#include "ScriptCompositeField.h"
#include "ui_ScriptPropertyRow.h"
#include <QLocale>
#include <QSignalBlocker>
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
std::vector<float*> Axes(GameSproutRuntime::ScriptValue& value){
    using GameSproutRuntime::ScriptValueKind;
    switch(value.Kind){
    case ScriptValueKind::Vector2:return {&value.Vector2.X,&value.Vector2.Y};
    case ScriptValueKind::Vector3:return {&value.Vector.X,&value.Vector.Y,&value.Vector.Z};
    case ScriptValueKind::Vector4:return {&value.Vector4.X,&value.Vector4.Y,&value.Vector4.Z,&value.Vector4.W};
    case ScriptValueKind::Quaternion:return {&value.Rotation.X,&value.Rotation.Y,&value.Rotation.Z,&value.Rotation.W};
    case ScriptValueKind::Color:return {&value.Tint.R,&value.Tint.G,&value.Tint.B,&value.Tint.A};
    default:return {};
    }
}
}
bool ScriptPropertyRow::SameDefinition(const GameSproutRuntime::ScriptFieldInfo& a,const GameSproutRuntime::ScriptFieldInfo& b){
    if(a.ID!=b.ID||a.Name!=b.Name||a.Default.Kind!=b.Default.Kind||a.ResourceKind!=b.ResourceKind||a.ComponentType!=b.ComponentType||a.TypeName!=b.TypeName||a.FixedLength!=b.FixedLength||a.Children.size()!=b.Children.size()||a.EnumValues.size()!=b.EnumValues.size())return false;
    for(std::size_t index=0;index<a.Children.size();++index)if(!SameDefinition(a.Children[index],b.Children[index]))return false;
    for(std::size_t index=0;index<a.EnumValues.size();++index){const auto& x=a.EnumValues[index];const auto& y=b.EnumValues[index];if(x.Name!=y.Name||x.Value!=y.Value||x.IsUnsigned!=y.IsUnsigned||x.UnsignedValue!=y.UnsignedValue)return false;}
    return true;
}
ScriptPropertyRow::ScriptPropertyRow(const ScriptFieldSnapshot& snapshot,const ScriptFieldTarget& target,const ComponentEditContext& context,QWidget* parent,const ScriptFieldPath& path,const std::string& rootID)
    :QWidget(parent),_Ui(new Ui::ScriptPropertyRowForm),_Field(snapshot.Info),_Value(snapshot.Value),_Target(target),_Context(context),_Path(path),_RootField(rootID.empty()?snapshot.Info.ID:rootID){
    _Ui->setupUi(this);
    setProperty("scriptFieldID",QString::fromStdString(snapshot.Info.ID));
    QString displayName=QString::fromStdString(_Field.Name.empty()?_Field.ID:_Field.Name);
    if(displayName.startsWith("m_"))displayName.remove(0,2);while(displayName.startsWith('_'))displayName.remove(0,1);
    _Ui->nameLabel->setText(displayName);_Ui->nameLabel->setToolTip(QString::fromStdString(_Field.TypeName.empty()?_Field.ID:_Field.TypeName));
    using GameSproutRuntime::ScriptValueKind;
    const auto kind=_Value.Kind;
    if(kind==ScriptValueKind::Structure||kind==ScriptValueKind::Sequence||kind==ScriptValueKind::Dictionary){
        _Ui->nameLabel->hide();_Ui->valueStack->setCurrentWidget(_Ui->compositePage);
        _Composite=new ScriptCompositeField(snapshot,target,context,path,_RootField,_Ui->compositePage);_Ui->compositePageLayout->addWidget(_Composite);
        _Composite->Changed=[this](const auto& path,const auto& value){const auto changed=PathChanged;if(changed)changed(path,value);};
        _Composite->ContainerChanged=[this](const auto& path,const auto& edit){const auto changed=ContainerChanged;if(changed)changed(path,edit);};
        _Composite->Invalid=[this](const auto& message){if(Invalid)Invalid(message);};
    }else if(!_Field.EnumValues.empty()&&(kind==ScriptValueKind::Integer||kind==ScriptValueKind::Unsigned)){
        _Ui->valueStack->setCurrentWidget(_Ui->enumPage);
        for(const auto& option:_Field.EnumValues)_Ui->enumCombo->addItem(QString::fromStdString(option.Name));
        QObject::connect(_Ui->enumCombo,qOverload<int>(&QComboBox::activated),this,[this](int index){
            if(index<0||std::size_t(index)>=_Field.EnumValues.size())return;
            auto value=_Value;const auto& option=_Field.EnumValues[index];
            if(value.Kind==GameSproutRuntime::ScriptValueKind::Unsigned)value.Unsigned=option.UnsignedValue;else value.Integer=option.Value;
            Dispatch(value);
        });
    }else if(kind==ScriptValueKind::Boolean)_Ui->valueStack->setCurrentWidget(_Ui->booleanPage);
    else if(!Axes(_Value).empty()){
        _Ui->valueStack->setCurrentWidget(_Ui->vectorPage);
        const auto count=Axes(_Value).size();_Ui->zLabel->setVisible(count>2);_Ui->zEdit->setVisible(count>2);_Ui->wLabel->setVisible(count>3);_Ui->wEdit->setVisible(count>3);
        if(kind==ScriptValueKind::Color){_Ui->xLabel->setText("R");_Ui->yLabel->setText("G");_Ui->zLabel->setText("B");_Ui->wLabel->setText("A");}
    }else if(kind==ScriptValueKind::Resource){
        _Ui->valueStack->setCurrentWidget(_Ui->resourcePage);_Ui->resourceField->Bind(context.Assets,context.Library,context.Selection,AssetType(_Field.ResourceKind));
        _Ui->resourceField->SetContextKey(context.Key("Script",_Field.ID));
    }else if(kind==ScriptValueKind::SceneObject||kind==ScriptValueKind::Component){
        _Ui->valueStack->setCurrentWidget(_Ui->objectPage);_ObjectField=new SceneObjectField(snapshot,target,context.Scripts,_Ui->objectPage);_Ui->objectLayout->addWidget(_ObjectField);
        _ObjectField->Changed=[this](const auto& value){Dispatch(value);};_ObjectField->Invalid=[this](const auto& error){if(Invalid)Invalid(error);};
    }else _Ui->valueStack->setCurrentWidget(_Ui->scalarPage);
    QObject::connect(_Ui->booleanCheck,&QCheckBox::clicked,this,[this]{Commit();});
    for(auto* edit:{_Ui->scalarEdit,_Ui->xEdit,_Ui->yEdit,_Ui->zEdit,_Ui->wEdit})QObject::connect(edit,&QLineEdit::editingFinished,this,[this,edit]{Commit(edit);});
    _Ui->resourceField->Changed=[this](const auto& reference){auto value=_Value;value.Resource=reference;Dispatch(value);};
    Load(snapshot);
}
ScriptPropertyRow::~ScriptPropertyRow(){Changed={};PathChanged={};ContainerChanged={};Invalid={};_Ui->resourceField->Changed={};delete _Ui;}
void ScriptPropertyRow::Dispatch(const GameSproutRuntime::ScriptValue& value){
    const auto pathChanged=PathChanged;const auto changed=Changed;if(pathChanged)pathChanged(_Path,value);else if(changed)changed(value);
}
bool ScriptPropertyRow::Latest(GameSproutRuntime::ScriptValue& value,std::string& error)const{
    value=_Value;
    // A multi-axis edit updates one axis of a fresh value rather than writing
    // back stale values for axes concurrently changed by the playing script.
    if(!_Context.Scripts)return true;
    ScriptPropertySnapshot snapshot;
    if(!_Context.Scripts->ReadFields(_Target.ObjectID,_Target.ScriptInstanceID,snapshot,error)||snapshot.Target!=_Target)return false;
    // Root ID is supplied by the component editor. Nested paths cannot be
    // resolved from their leaf's repeated ID (e.g. every vector element).
    const auto& root=_RootField.empty()?_Field.ID:_RootField;
    for(const auto& field:snapshot.Fields)if(field.Info.ID==root){
        const auto* current=&field.Value;
        for(const auto& step:_Path){
            if(step.Kind==ScriptPathKind::Member){const auto found=current->Fields.find(step.Member);if(found==current->Fields.end())return false;current=&found->second;}
            else if(step.Kind==ScriptPathKind::Element){if(step.Index>=current->Elements.size())return false;current=&current->Elements[step.Index];}
            else {if(step.Index>=current->Entries.size())return false;current=step.Kind==ScriptPathKind::EntryKey?&current->Entries[step.Index].first:&current->Entries[step.Index].second;}
        }
        value=*current;return true;
    }
    error="脚本字段已经变更。";return false;
}
void ScriptPropertyRow::Commit(QLineEdit* changed){
    auto value=_Value;bool ok=true;using GameSproutRuntime::ScriptValueKind;
    switch(value.Kind){
    case ScriptValueKind::Boolean:value.Boolean=_Ui->booleanCheck->isChecked();break;
    case ScriptValueKind::String:value.String=_Ui->scalarEdit->text().toStdString();ok=value.String.find('\0')==std::string::npos;break;
    case ScriptValueKind::Integer:value.Integer=_Ui->scalarEdit->text().trimmed().toLongLong(&ok,10);break;
    case ScriptValueKind::Unsigned:value.Unsigned=_Ui->scalarEdit->text().trimmed().toULongLong(&ok,10);ok=ok&&!_Ui->scalarEdit->text().trimmed().startsWith('-');break;
    case ScriptValueKind::Number:value.Number=QLocale::c().toDouble(_Ui->scalarEdit->text().trimmed(),&ok);ok=ok&&std::isfinite(value.Number);break;
    default:{
        if(Axes(value).empty())return;
        std::string error;if(!Latest(value,error)){if(Invalid)Invalid(error.empty()?"字段会话或容器项已经改变。":error);return;}
        const auto axes=Axes(value);QLineEdit* edits[]={_Ui->xEdit,_Ui->yEdit,_Ui->zEdit,_Ui->wEdit};
        for(std::size_t index=0;index<axes.size();++index)if(edits[index]==changed){bool valid=false;const double number=QLocale::c().toDouble(changed->text().trimmed(),&valid);if(!valid||!std::isfinite(number)||std::fabs(number)>std::numeric_limits<float>::max())ok=false;else *axes[index]=float(number);}
        break;
    }}
    if(!ok){if(Invalid)Invalid("字段值无效，请输入与字段类型一致的有限数值。");return;}
    if(changed)changed->setModified(false);Dispatch(value);
}
void ScriptPropertyRow::Load(const ScriptFieldSnapshot& snapshot){
    _Value=snapshot.Value;const auto& value=_Value;
    const auto text=[](QLineEdit* edit,const QString& value){if(!edit->hasFocus()||!edit->isModified()){edit->setText(value);if(!edit->hasFocus())edit->setCursorPosition(0);}};
    using GameSproutRuntime::ScriptValueKind;
    if(_Composite){_Composite->Load(snapshot);return;}
    if(!_Field.EnumValues.empty()&&(value.Kind==ScriptValueKind::Integer||value.Kind==ScriptValueKind::Unsigned)){
        int selected=-1;for(std::size_t index=0;index<_Field.EnumValues.size();++index){const auto& option=_Field.EnumValues[index];if(value.Kind==ScriptValueKind::Unsigned?value.Unsigned==option.UnsignedValue:value.Integer==option.Value){selected=int(index);break;}}
        while(_Ui->enumCombo->count()>int(_Field.EnumValues.size()))_Ui->enumCombo->removeItem(_Ui->enumCombo->count()-1);
        if(selected<0){selected=_Ui->enumCombo->count();_Ui->enumCombo->addItem(tr("未知值 (%1)").arg(value.Kind==ScriptValueKind::Unsigned?QString::number(value.Unsigned):QString::number(value.Integer)));}
        const QSignalBlocker block(_Ui->enumCombo);_Ui->enumCombo->setCurrentIndex(selected);return;
    }
    if(value.Kind==ScriptValueKind::Boolean){const QSignalBlocker block(_Ui->booleanCheck);_Ui->booleanCheck->setChecked(value.Boolean);}
    else if(const auto axes=Axes(_Value);!axes.empty()){QLineEdit* edits[]={_Ui->xEdit,_Ui->yEdit,_Ui->zEdit,_Ui->wEdit};for(std::size_t index=0;index<axes.size();++index)text(edits[index],QString::number(*axes[index],'g',9));}
    else if(value.Kind==ScriptValueKind::Resource)_Ui->resourceField->SetValue(value.Resource);
    else if(value.Kind==ScriptValueKind::SceneObject||value.Kind==ScriptValueKind::Component){if(_ObjectField)_ObjectField->Load(snapshot,_Target);}
    else text(_Ui->scalarEdit,value.Kind==ScriptValueKind::String?QString::fromStdString(value.String):value.Kind==ScriptValueKind::Integer?QString::number(value.Integer):value.Kind==ScriptValueKind::Unsigned?QString::number(value.Unsigned):QString::number(value.Number,'g',17));
}

void ScriptPropertyRow::AttachItemControls(QWidget* controls){
    if(_Composite)_Composite->AttachItemControls(controls);
    else {controls->setParent(this);_Ui->layout->addWidget(controls);}
}

void ScriptPropertyRow::SaveExpansion(std::map<std::string,bool>& values)const{if(_Composite)_Composite->SaveExpansion(values);}
void ScriptPropertyRow::RestoreExpansion(const std::map<std::string,bool>& values){if(_Composite)_Composite->RestoreExpansion(values);}
