#include "SceneObjectField.h"
#include "../Shared/SceneObjectDragPayload.h"
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
    const bool assigned=_Value.SceneObject.Assigned;
    _DisplayName=!assigned?tr("None (GameObject)"):field.MissingObject?tr("Missing (GameObject)"):
        field.ObjectName.empty()?tr("GameObject"):QString::fromStdString(field.ObjectName);
    const QString path=QString::fromStdString(field.ObjectPath);
    _Ui->objectNameEdit->setToolTip(!assigned?_Ui->objectNameEdit->toolTip():field.MissingObject?
        tr("引用的对象已缺失。撤销删除后可以恢复；点击 × 可永久清空此引用。\nFileID: %1").arg(_Value.SceneObject.ObjectFileID):path);
    _Ui->locateObjectButton->setEnabled(_Scripts&&assigned&&!field.MissingObject);
    _Ui->clearObjectButton->setEnabled(_Scripts&&assigned);
    _Ui->pickObjectButton->setEnabled(_Scripts!=nullptr);
    UpdateName();
    QObject::connect(_Ui->pickObjectButton,&QToolButton::clicked,this,[this]{OpenPicker();});
    QObject::connect(_Ui->clearObjectButton,&QToolButton::clicked,this,[this]{
        auto value=_Value;value.SceneObject={};
        const auto changed=Changed;if(changed)changed(value);
    });
    QObject::connect(_Ui->locateObjectButton,&QToolButton::clicked,this,[this]{
        std::string error;
        if(!_Scripts||!_Scripts->LocateObjectReference(_Target,_Value,error))
            Fail(error.empty()?"脚本字段服务尚未就绪。":error);
    });
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
bool SceneObjectField::Decode(const QMimeData* mime,GameSproutRuntime::ScriptValue& value,std::string& error) const {
    SceneObjectDragPayload::Value reference;
    if(!SceneObjectDragPayload::Decode(mime,reference)) {error="请从 Hierarchy 拖入一个场景对象。";return false;}
    if(!_Scripts) {error="脚本字段服务尚未就绪。";return false;}
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
    if(!Decode(event->mimeData(),value,error)) {event->ignore();Fail(error);return;}
    event->setDropAction(Qt::CopyAction);event->accept();
    const auto changed=Changed;if(changed)changed(value);
}
void SceneObjectField::OpenPicker() {
    if(!_Scripts) {Fail("脚本字段服务尚未就绪。");return;}
    std::vector<ScriptObjectChoice> choices;std::string error;
    if(!_Scripts->GetSceneObjectChoices(_Target,choices,error)) {Fail(error);return;}
    delete _Picker;delete _PickerUi;
    _Picker=new QDialog(this);_PickerUi=new Ui::SceneObjectPickerForm;_PickerUi->setupUi(_Picker);
    _Picker->setWindowModality(Qt::WindowModal);
    auto* none=new QTreeWidgetItem(_PickerUi->objectTree,{tr("None (GameObject)")});
    none->setData(0,Qt::UserRole,false);
    if(!_Value.SceneObject.Assigned)_PickerUi->objectTree->setCurrentItem(none);
    for(const auto& choice:choices) {
        const QString path=QString::fromStdString(choice.Path.empty()?choice.Name:choice.Path);
        auto* item=new QTreeWidgetItem(_PickerUi->objectTree,{path});
        item->setData(0,Qt::UserRole,true);
        item->setData(0,Qt::UserRole+1,QVariant::fromValue<qulonglong>(choice.ObjectFileID));
        item->setToolTip(0,path);
        if(_Value.SceneObject.Assigned&&choice.ObjectFileID==_Value.SceneObject.ObjectFileID)
            _PickerUi->objectTree->setCurrentItem(item);
    }
    QObject::connect(_PickerUi->searchEdit,&QLineEdit::textChanged,_Picker,[this](const QString& search){
        for(int index=0;index<_PickerUi->objectTree->topLevelItemCount();++index) {
            auto* item=_PickerUi->objectTree->topLevelItem(index);
            item->setHidden(!item->text(0).contains(search,Qt::CaseInsensitive));
        }
    });
    const auto accept=[this]{
        const auto* item=_PickerUi->objectTree->currentItem();
        if(!item||item->isHidden()) {Fail("请选择一个场景对象。");return;}
        auto value=_Value;
        value.SceneObject={item->data(0,Qt::UserRole).toBool(),item->data(0,Qt::UserRole+1).toULongLong()};
        _Picker->accept();
        const auto changed=Changed;if(changed)changed(value);
    };
    QObject::connect(_PickerUi->selectButton,&QPushButton::clicked,_Picker,accept);
    QObject::connect(_PickerUi->objectTree,&QTreeWidget::itemDoubleClicked,_Picker,[accept](QTreeWidgetItem*,int){accept();});
    QObject::connect(_PickerUi->cancelButton,&QPushButton::clicked,_Picker,&QDialog::reject);
    _Picker->open();_PickerUi->searchEdit->setFocus();
}
