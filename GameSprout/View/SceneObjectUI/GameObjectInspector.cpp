#include "GameObjectInspector.h"
#include "ComponentInfoSection.h"
#include "AddComponentPopup.h"
#include "IScriptEditor.h"
#include "IObjectEditing.h"
#include "IAssetsManager.h"
#include "../Shared/AssetDragPayload.h"
#include "ui_GameObjectInspector.h"
#include "ui_LayerChildrenDialog.h"
#include "LayerMaskCombo.h"
#include <QDialog>
#include <QComboBox>
#include <QToolButton>
#include <limits>
#include <QApplication>
#include <QSignalBlocker>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <map>

GameObjectInspector::GameObjectInspector(QWidget* parent) : QWidget(parent), _Ui(new Ui::GameObjectInspectorForm()) {
    _Ui->setupUi(this);
    setAcceptDrops(true);
    QObject::connect(_Ui->addComponentButton,&QPushButton::clicked,this,[this]{OpenAddComponent();});
    QObject::connect(_Ui->tagCombo,QOverload<int>::of(&QComboBox::activated),this,[this](int index){CommitTag(index);});
    QObject::connect(_Ui->layerCombo,QOverload<int>::of(&QComboBox::activated),this,[this](int index){CommitLayer(index);});
    QObject::connect(_Ui->classificationSettingsButton,&QToolButton::clicked,this,[this]{if(OpenProjectSettings)OpenProjectSettings();});
    QObject::connect(_Ui->objectNameEdit,&QLineEdit::editingFinished,this,[this]{CommitName();});
    QObject::connect(_Ui->objectActiveCheck,&QCheckBox::clicked,this,[this](bool active){CommitActive(active);});
    QObject::connect(_Ui->objectLockButton,&QToolButton::toggled,this,[this](bool locked) {
        if (LockChanged) LockChanged(locked);
    });
}
GameObjectInspector::~GameObjectInspector() { if(_AddPopup)delete _AddPopup;LockChanged={};OpenProjectSettings={};delete _Ui; }
QWidget* GameObjectInspector::HeaderExtensionsHost() const {return _Ui->objectHeaderExtensions;}
void GameObjectInspector::SetLocked(bool locked) {
    const QSignalBlocker blocker(_Ui->objectLockButton);
    _Ui->objectLockButton->setChecked(locked);
}
void GameObjectInspector::SetDebugMode(bool debug) {
    _DebugMode=debug;
    _Ui->componentScroll->setVisible(!_SceneRoot || debug);
    _Ui->emptySceneSpace->setVisible(_SceneRoot && !debug);
    _Ui->addComponentButton->setVisible(!_SceneRoot);
    _Ui->classificationRow->setVisible(!_SceneRoot);
    for (auto* section:_Sections) {
        section->SetDebugMode(debug);
        section->setVisible(!_SceneRoot || debug);
    }
}
void GameObjectInspector::Clear() {
    if(_AddPopup)delete _AddPopup;
    _Loading=true;_Context={};_LoadedID=0;_SceneRoot=false;setEnabled(false);
    _Ui->tagCombo->clear();_Ui->layerCombo->clear();
    _Ui->objectNameEdit->clear();_Ui->objectActiveCheck->setChecked(false);
    for(auto* section:_Sections) delete section;
    _Sections.clear();
    SetDebugMode(_DebugMode);
    _Loading=false;
}
void GameObjectInspector::Load(GameSproutRuntime::IGameObject& object) {
    std::map<std::uint64_t,bool> expanded;
    if(_LoadedID==object.GetInstanceID())for(auto* section:_Sections)
        if(section->GetComponentInstanceID())expanded[section->GetComponentInstanceID()]=section->IsExpanded();
    if (_LoadedID!=object.GetInstanceID() && _AddPopup) delete _AddPopup;
    _Loading=true;setEnabled(true);
    struct Guard { bool& Value;~Guard(){Value=false;} } guard{_Loading};
    if (_LoadedID!=object.GetInstanceID() || QApplication::focusWidget()!=_Ui->objectNameEdit || !_Ui->objectNameEdit->isModified()) {
        _Ui->objectNameEdit->setText(QString::fromStdString(object.GetName()));
        if(QApplication::focusWidget()!=_Ui->objectNameEdit)_Ui->objectNameEdit->setCursorPosition(0);
        _Ui->objectNameEdit->setToolTip(QString::fromStdString(object.GetName()));
    }
    _LoadedID=object.GetInstanceID();
    _SceneRoot=object.GetScene()==&object;
    SetDebugMode(_DebugMode);
    RefreshClassification();
    _Ui->objectActiveCheck->setVisible(!_SceneRoot);
    _Ui->objectActiveCheck->setChecked(object.IsActiveSelf());
    while(_Sections.size()>object.GetComponentCount()) {delete _Sections.back();_Sections.pop_back();}
    for(std::size_t index=0;index<object.GetComponentCount();++index) {
        auto* component=object.GetComponentAt(index);
        if(!component) continue;
        if(index==_Sections.size()) {
            auto* section=new ComponentInfoSection(_Ui->componentHost);
            _Ui->componentLayout->insertWidget(_Ui->componentLayout->count()-2,section);
            _Sections.push_back(section);
        }
        auto componentContext=_Context;
        componentContext.ComponentID=component->GetComponentInstanceID();
        _Sections[index]->Bind(componentContext);
        _Sections[index]->Load(*component);
        if(componentContext.ComponentID) {
            const auto previous=expanded.find(componentContext.ComponentID);
            _Sections[index]->SetExpanded(previous==expanded.end()?true:previous->second);
        }
        _Sections[index]->SetEditability(!_ReadOnly,!_StructureReadOnly);
        _Sections[index]->SetDebugMode(_DebugMode);
        _Sections[index]->setVisible(!_SceneRoot || _DebugMode);
    }
}

void GameObjectInspector::OpenAddComponent() {
    if (_StructureReadOnly || _SceneRoot || !_Context.Resolve()) return;
    if (_AddPopup) delete _AddPopup;
    _AddPopup=new AddComponentPopup(_Context,this);
    _AddPopup->OpenAt(_Ui->addComponentButton);
}
bool GameObjectInspector::AttachDroppedScripts(const QMimeData* mime,bool commit) {
    if (_StructureReadOnly || _SceneRoot || !_Context.Scripts || !_Context.Scenes || !_Context.Resolve()) return false;
    std::vector<ProjectAssetSelection> selection;
    if (!AssetDragPayload::Decode(mime,selection) || selection.size()!=1 || selection.front().IsSubAsset) return false;
    const auto types=_Context.Scripts->GetScriptTypes();
    std::vector<EditorScriptType> matching;
    for (const auto& type:types) if(type.Source.Guid==selection.front().GuidString) matching.push_back(type);
    if (matching.empty()) {
        if (commit && _Context.Report) _Context.Report("此脚本尚未成功编译，请先编译项目脚本。");
        return false;
    }
    if (!commit) return true;
    if (matching.size()!=1) { OpenAddComponent();if(_Context.Report)_Context.Report("此源文件包含多个脚本类型，请在添加组件中选择。");return true; }
    std::string error;
    if(!_Context.Scenes->AttachScriptComponent(_Context.ObjectID,matching.front().Type.TypeID,matching.front().Source,error)) {
        if(_Context.Report)_Context.Report(error);return false;
    }
    return true;
}
void GameObjectInspector::dragEnterEvent(QDragEnterEvent* event) { if(AttachDroppedScripts(event->mimeData(),false))event->acceptProposedAction(); }
void GameObjectInspector::dragMoveEvent(QDragMoveEvent* event) { if(AttachDroppedScripts(event->mimeData(),false))event->acceptProposedAction(); }
void GameObjectInspector::dropEvent(QDropEvent* event) { if(AttachDroppedScripts(event->mimeData(),true))event->acceptProposedAction(); }

void GameObjectInspector::CommitName() {
    if (_Loading || _ReadOnly) return;
    auto* object=_Context.Resolve();
    if (!object) return;
    const std::string previous=object->GetName();
    const std::string next=_Ui->objectNameEdit->text().toStdString();
    if (next==previous) return;
    if (next.empty() || next.size()>1024 || next.find('\0')!=std::string::npos) {
        if (_Context.Report) _Context.Report("节点名称须包含 1..1024 字节且不含 NUL。");
        _Ui->objectNameEdit->setText(QString::fromStdString(previous));return;
    }
    const auto operation=[](std::string value) {
        return [value](GameSproutRuntime::IGameObject& target,std::string& error) {
            target.SetName(value);
            if(target.GetName()==value)return true;
            error="Runtime 拒绝修改节点名称。";return false;
        };
    };
    _Context.Execute("修改节点名称",operation(next),operation(previous));
    object=_Context.Resolve();
    if(object)_Ui->objectNameEdit->setText(QString::fromStdString(object->GetName()));
}
void GameObjectInspector::CommitActive(bool active) {
    if (_Loading || _ReadOnly) return;
    auto* object=_Context.Resolve();
    if (!object) return;
    const bool previous=object->IsActiveSelf();
    if(previous==active)return;
    const auto operation=[](bool value) {
        return [value](GameSproutRuntime::IGameObject& target,std::string& error) {
            target.SetActive(value);
            if(target.IsActiveSelf()==value)return true;
            error="Runtime 拒绝修改节点启用状态。";return false;
        };
    };
    _Context.Execute("修改节点启用状态",operation(active),operation(previous));
    object=_Context.Resolve();
    if(object)_Ui->objectActiveCheck->setChecked(object->IsActiveSelf());
}

void GameObjectInspector::OnAnimationEvent(const GameSproutRuntime::AnimationNotification& event) {
    for(auto* section:_Sections)section->OnAnimationEvent(event);
}

void GameObjectInspector::SetReadOnly(bool value){SetEditability(!value,!value);}
void GameObjectInspector::SetEditability(bool properties,bool structure) {
    _ReadOnly=!properties;_StructureReadOnly=!structure;
    if(!structure&&_AddPopup)delete _AddPopup;
    _Ui->objectNameEdit->setReadOnly(!properties);
    _Ui->objectActiveCheck->setEnabled(properties);
    _Ui->tagCombo->setEnabled(properties);_Ui->layerCombo->setEnabled(properties);
    _Ui->classificationSettingsButton->setEnabled(structure);
    _Ui->addComponentButton->setEnabled(structure);
    setAcceptDrops(structure);
    for(auto* section:_Sections)section->SetEditability(properties,structure);
}

void GameObjectInspector::SetProjectSettings(const GameSproutRuntime::RuntimeProjectSettings& settings) {
    _ProjectSettings=settings;if(_LoadedID) {
        RefreshClassification();
        for(auto* widget:findChildren<QWidget*>())if(auto* mask=dynamic_cast<LayerMaskCombo*>(widget))mask->SetLayers(settings.LayerNames);
    }
}
void GameObjectInspector::RefreshClassification() {
    const QSignalBlocker tagBlock(_Ui->tagCombo),layerBlock(_Ui->layerCombo);
    _Ui->tagCombo->clear();_Ui->layerCombo->clear();
    auto* object=_Context.Resolve();if(!object)return;
    const auto tagID=object->GetTagID();const auto layer=object->GetLayer();
    int selectedTag=-1,selectedLayer=-1;
    for(const auto& tag:_ProjectSettings.Tags) {
        const int index=_Ui->tagCombo->count();
        _Ui->tagCombo->addItem(QString::fromStdString(tag.Name),QVariant::fromValue<qulonglong>(tag.ID));
        if(tag.ID==tagID)selectedTag=index;
    }
    if(selectedTag<0){selectedTag=_Ui->tagCombo->count();_Ui->tagCombo->addItem(QStringLiteral("Missing Tag"),QVariant::fromValue<qulonglong>(tagID));}
    for(std::uint8_t index=0;index<32;++index)if(!_ProjectSettings.LayerNames[index].empty()||index==layer) {
        const auto& name=_ProjectSettings.LayerNames[index];
        if(index==layer)selectedLayer=_Ui->layerCombo->count();
        _Ui->layerCombo->addItem(name.empty()?QStringLiteral("未命名 Layer %1").arg(index):QString::fromStdString(name),index);
    }
    _Ui->tagCombo->setCurrentIndex(selectedTag);_Ui->layerCombo->setCurrentIndex(selectedLayer);
    _Ui->tagCombo->setToolTip(QString::fromStdString(object->GetTag()));
    _Ui->layerCombo->setToolTip(QStringLiteral("Layer %1").arg(layer));
}
void GameObjectInspector::CommitTag(int index) {
    if(_Loading||_ReadOnly||_SceneRoot||index<0)return;
    auto* object=_Context.Resolve();if(!object)return;
    const auto previous=object->GetTagID();const auto next=_Ui->tagCombo->itemData(index).toULongLong();
    if(previous==next)return;
    const auto change=[](std::uint64_t id){return [id](GameSproutRuntime::IGameObject& target,std::string& error){
        if(target.SetTagID(id))return true;error="Runtime 拒绝修改 Tag，定义可能已经删除。";return false;
    };};
    _Context.Execute("修改对象 Tag",change(next),change(previous));RefreshClassification();
}
void GameObjectInspector::CommitLayer(int index) {
    if(_Loading||_ReadOnly||_SceneRoot||index<0)return;
    auto* object=_Context.Resolve();if(!object)return;
    const auto next=static_cast<std::uint8_t>(_Ui->layerCombo->itemData(index).toUInt());
    const auto previous=object->GetLayer();if(previous==next)return;
    const ComponentEditContext context=_Context;
    bool children=false;
    if(object->GetChildCount()) {
        QPointer<GameObjectInspector> self(this);QPointer<QDialog> dialog=new QDialog(this);
        Ui::LayerChildrenDialogForm form;form.setupUi(dialog);
        QObject::connect(form.selfButton,&QPushButton::clicked,dialog,[dialog]{if(dialog)dialog->done(1);});
        QObject::connect(form.childrenButton,&QPushButton::clicked,dialog,[dialog]{if(dialog)dialog->done(2);});
        QObject::connect(form.cancelButton,&QPushButton::clicked,dialog,&QDialog::reject);
        const int choice=dialog->exec();delete dialog.data();if(!self)return;
        if(choice!=1&&choice!=2){RefreshClassification();return;}children=choice==2;
    }
    std::string error;
    if(!context.Resolve()||!context.Scenes)error="对象或场景已改变。";
    else if(context.Editing)context.Editing->SetLayer({context.SceneID,context.ObjectID},next,children,error);
    else if(!context.Scenes->IsPlaySessionActive())context.Scenes->SetObjectLayer(context.ObjectID,next,children,error);
    else error="运行属性编辑服务尚未就绪。";
    if(context.Report)context.Report(error);
    RefreshClassification();
}
