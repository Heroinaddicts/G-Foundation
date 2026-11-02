#include "SceneObjectField.h"
#include "../Shared/SceneObjectDragPayload.h"
#include "../Shared/SceneComponentDragPayload.h"
#include "ui_SceneObjectField.h"
#include "ui_SceneObjectPicker.h"
#include <QDialog>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QResizeEvent>
#include <algorithm>

SceneObjectField::SceneObjectField(const ScriptFieldSnapshot& field,const ScriptFieldTarget& target,
    IScriptEditor* scripts,QWidget* parent)
    :QWidget(parent),_Ui(new Ui::SceneObjectFieldForm),_Scripts(scripts),_Target(target),_Value(field.Value) {
    setProperty("missingReference",field.MissingObject);
    _Ui->setupUi(this);
    Load(field,target);
    QObject::connect(_Ui->pickObjectButton,&QToolButton::clicked,this,[this]{OpenPicker();});
    QObject::connect(_Ui->clearObjectButton,&QToolButton::clicked,this,[this]{
        auto value=_Value;value.SceneObject={};value.Component={};
        const auto changed=Changed;if(changed)changed(value);
    });
    QObject::connect(_Ui->locateObjectButton,&QToolButton::clicked,this,[this]{
        std::string error;
        if(!_Scripts||!(_Value.Kind==GameSproutRuntime::ScriptValueKind::Component?_Scripts->LocateComponentReference(_Target,_Value,error):_Scripts->LocateObjectReference(_Target,_Value,error)))
            Fail(error.empty()?"脚本字段服务尚未就绪。":error);
    });
}
void SceneObjectField::Load(const ScriptFieldSnapshot& field,const ScriptFieldTarget& target) {
    if(_Target!=target){delete _Picker;_Picker=nullptr;delete _PickerUi;_PickerUi=nullptr;}
    _Target=target;_Value=field.Value;_Info=field.Info;
    setProperty("missingReference",field.MissingObject);
    const bool component=_Value.Kind==GameSproutRuntime::ScriptValueKind::Component;
    const bool assigned=component?_Value.Component.Assigned:_Value.SceneObject.Assigned;
    const QString type=component?QString::fromStdString(_Info.TypeName.empty()?"Component":_Info.TypeName):QStringLiteral("GameObject");
    _DisplayName=!assigned?tr("None (%1)").arg(type):field.MissingObject?tr("Missing (%1)").arg(type):
        field.ObjectName.empty()?tr("GameObject"):QString::fromStdString(field.ObjectName);
    _Ui->objectNameEdit->setToolTip(!assigned?tr("拖入场景对象或组件标题，或点击圆形按钮选择。"):
        field.MissingObject?tr("引用的对象已缺失；点击 × 可清空此引用。"):QString::fromStdString(field.ObjectPath));
    _Ui->locateObjectButton->setEnabled(_Scripts&&assigned&&!field.MissingObject);
    _Ui->clearObjectButton->setEnabled(_Scripts&&assigned);_Ui->pickObjectButton->setEnabled(_Scripts!=nullptr);
    UpdateName();
}
SceneObjectField::~SceneObjectField() {
    Changed={};Invalid={};
    delete _Picker;delete _PickerUi;delete _Ui;
}
void SceneObjectField::UpdateName() {
    const int width=std::max(16,_Ui->objectNameEdit->contentsRect().width()-12);
    _Ui->objectNameEdit->setText(_Ui->objectNameEdit->fontMetrics().elidedText(_DisplayName,Qt::ElideRight,width));
    _Ui->objectNameEdit->setCursorPosition(0);
}
void SceneObjectField::resizeEvent(QResizeEvent* event) {QWidget::resizeEvent(event);UpdateName();}
void SceneObjectField::Fail(const std::string& error) {
    if(_PickerUi)_PickerUi->errorLabel->setText(QString::fromStdString(error));
    if(Invalid)Invalid(error);
}
bool SceneObjectField::Decode(const QMimeData* mime,GameSproutRuntime::ScriptValue& value,std::string& error,bool assign) const {
    if(!_Scripts) {error="脚本字段服务尚未就绪。";return false;}
    if(_Value.Kind==GameSproutRuntime::ScriptValueKind::Component){
        SceneComponentDragPayload::Value component;
        const bool explicitComponent=SceneComponentDragPayload::Decode(mime,component);
        if(!assign){
            SceneObjectDragPayload::Value object;
            if(!SceneObjectDragPayload::Decode(mime,object)){error="请拖入一个场景对象或组件标题。";return false;}
            if(object.SessionID!=_Target.SessionID||object.WorldID!=_Target.WorldID){error="拖入对象所属会话已经失效。";return false;}
            std::vector<ScriptComponentChoice> choices;
            if(!_Scripts->GetSceneComponentChoices(_Target,_Info.ComponentType,choices,error))return false;
            std::size_t count=0;
            for(const auto& choice:choices)if(choice.Object.ObjectID==object.ObjectID&&(!explicitComponent||choice.ComponentInstanceID==component.ComponentID))++count;
            if(count!=1){error=count?"对象上有多个匹配组件，请拖入具体组件标题或使用选择器。":"没有符合要求的组件。";return false;}
            return true;
        }
        if(explicitComponent)return _Scripts->ReferenceFromComponent(_Target,
            {component.Object.SessionID,component.Object.WorldID,component.Object.ObjectID},component.ComponentID,_Info.ComponentType,value,error);
        SceneObjectDragPayload::Value object;
        if(!SceneObjectDragPayload::Decode(mime,object)){error="请拖入一个场景对象或组件标题。";return false;}
        return _Scripts->ComponentReferenceFromObject(_Target,{object.SessionID,object.WorldID,object.ObjectID},_Info.ComponentType,value,error);
    }
    SceneObjectDragPayload::Value reference;
    if(!SceneObjectDragPayload::Decode(mime,reference)) {error="请从 Hierarchy 拖入一个场景对象。";return false;}
    return _Scripts->ReferenceFromObject(_Target,
        {reference.SessionID,reference.WorldID,reference.ObjectID},value,error);
}
void SceneObjectField::dragEnterEvent(QDragEnterEvent* event) {
    GameSproutRuntime::ScriptValue value;std::string error;
    if(Decode(event->mimeData(),value,error)) {event->setDropAction(Qt::CopyAction);event->accept();}
    else event->ignore();
}
void SceneObjectField::dragMoveEvent(QDragMoveEvent* event) {
    GameSproutRuntime::ScriptValue value;std::string error;
    if(Decode(event->mimeData(),value,error)) {event->setDropAction(Qt::CopyAction);event->accept();}
    else event->ignore();
}
void SceneObjectField::dropEvent(QDropEvent* event) {
    GameSproutRuntime::ScriptValue value;std::string error;
    if(!Decode(event->mimeData(),value,error,true)) {event->ignore();Fail(error);return;}
    event->setDropAction(Qt::CopyAction);event->accept();
    const auto changed=Changed;if(changed)changed(value);
}
void SceneObjectField::OpenPicker() {
    if(!_Scripts) {Fail("脚本字段服务尚未就绪。");return;}
    const bool component=_Value.Kind==GameSproutRuntime::ScriptValueKind::Component;
    std::vector<ScriptObjectChoice> objects;std::vector<ScriptComponentChoice> components;std::string error;
    if(component?!_Scripts->GetSceneComponentChoices(_Target,_Info.ComponentType,components,error):!_Scripts->GetSceneObjectChoices(_Target,objects,error)){Fail(error);return;}
    delete _Picker;delete _PickerUi;
    _Picker=new QDialog(this);_PickerUi=new Ui::SceneObjectPickerForm;_PickerUi->setupUi(_Picker);
    _Picker->setWindowTitle(component?tr("选择组件"):tr("选择 GameObject"));
    _Picker->setWindowModality(Qt::WindowModal);
    auto* none=new QTreeWidgetItem(_PickerUi->objectTree,{tr("None")});none->setData(0,Qt::UserRole,false);none->setData(0,Qt::UserRole+10,-1);
    _PickerUi->objectTree->setCurrentItem(none);
    const auto add=[this](const std::string& name,std::size_t index){const auto path=QString::fromStdString(name);auto* item=new QTreeWidgetItem(_PickerUi->objectTree,{path});item->setData(0,Qt::UserRole,true);item->setData(0,Qt::UserRole+10,int(index));item->setToolTip(0,path);return item;};
    if(component)for(std::size_t index=0;index<components.size();++index){
        const auto& choice=components[index];auto* item=add(choice.Path,index);
        if(_Value.Component.Assigned&&((_Value.Component.ComponentFileID&&choice.ComponentFileID==_Value.Component.ComponentFileID)||
            (_Value.Component.RuntimeSceneID==choice.Object.WorldID&&_Value.Component.RuntimeObjectID==choice.Object.ObjectID&&_Value.Component.RuntimeComponentID==choice.ComponentInstanceID)))_PickerUi->objectTree->setCurrentItem(item);
    }else for(std::size_t index=0;index<objects.size();++index){
        const auto& choice=objects[index];auto* item=add(choice.Path.empty()?choice.Name:choice.Path,index);
        item->setData(0,Qt::UserRole+1,QVariant::fromValue<qulonglong>(choice.ObjectFileID));
        if(_Value.SceneObject.Assigned&&_Value.SceneObject.ObjectFileID==choice.ObjectFileID&&_Value.SceneObject.RuntimeSceneID==choice.RuntimeSceneID&&_Value.SceneObject.RuntimeObjectID==choice.RuntimeObjectID)_PickerUi->objectTree->setCurrentItem(item);
    }
    QObject::connect(_PickerUi->searchEdit,&QLineEdit::textChanged,_Picker,[this](const QString& search){
        for(int index=0;index<_PickerUi->objectTree->topLevelItemCount();++index){auto* item=_PickerUi->objectTree->topLevelItem(index);item->setHidden(!item->text(0).contains(search,Qt::CaseInsensitive));}
    });
    const auto accept=[this,component,objects,components]{
        const auto* item=_PickerUi->objectTree->currentItem();if(!item||item->isHidden()){Fail("请选择一个引用目标。");return;}
        const int index=item->data(0,Qt::UserRole+10).toInt();auto value=_Value;std::string error;
        if(index<0){value.SceneObject={};value.Component={};}
        else if(component){
            if(std::size_t(index)>=components.size()){Fail("组件候选已经改变。");return;}
            const auto& choice=components[index];
            if(!_Scripts->ReferenceFromComponent(_Target,choice.Object,choice.ComponentInstanceID,_Info.ComponentType,value,error)){Fail(error);return;}
        }else {
            if(std::size_t(index)>=objects.size()){Fail("对象候选已经改变。");return;}
            const auto& choice=objects[index];
            value.SceneObject={true,choice.ObjectFileID,choice.RuntimeSceneID,choice.RuntimeObjectID};
        }
        _Picker->accept();const auto changed=Changed;if(changed)changed(value);
    };
    QObject::connect(_PickerUi->selectButton,&QPushButton::clicked,_Picker,accept);
    QObject::connect(_PickerUi->objectTree,&QTreeWidget::itemDoubleClicked,_Picker,[accept](QTreeWidgetItem*,int){accept();});
    QObject::connect(_PickerUi->cancelButton,&QPushButton::clicked,_Picker,&QDialog::reject);
    _Picker->open();_PickerUi->searchEdit->setFocus();
}
