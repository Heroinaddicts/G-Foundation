#include "GameObjectInspector.h"
#include "ComponentInfoSection.h"
#include "ui_GameObjectInspector.h"
#include <QApplication>
#include <QSignalBlocker>

GameObjectInspector::GameObjectInspector(QWidget* parent) : QWidget(parent), _Ui(new Ui::GameObjectInspectorForm()) {
    _Ui->setupUi(this);
    QObject::connect(_Ui->objectNameEdit,&QLineEdit::editingFinished,this,[this]{CommitName();});
    QObject::connect(_Ui->objectActiveCheck,&QCheckBox::clicked,this,[this](bool active){CommitActive(active);});
    QObject::connect(_Ui->objectLockButton,&QToolButton::toggled,this,[this](bool locked) {
        if (LockChanged) LockChanged(locked);
    });
}
GameObjectInspector::~GameObjectInspector() { LockChanged={};delete _Ui; }
void GameObjectInspector::SetLocked(bool locked) {
    const QSignalBlocker blocker(_Ui->objectLockButton);
    _Ui->objectLockButton->setChecked(locked);
}
void GameObjectInspector::SetDebugMode(bool debug) {
    _DebugMode=debug;
    _Ui->componentScroll->setVisible(!_SceneRoot || debug);
    _Ui->emptySceneSpace->setVisible(_SceneRoot && !debug);
    for (auto* section:_Sections) {
        section->SetDebugMode(debug);
        section->setVisible(!_SceneRoot || debug);
    }
}
void GameObjectInspector::Clear() {
    _Loading=true;_Context={};_LoadedID=0;_SceneRoot=false;setEnabled(false);
    _Ui->objectNameEdit->clear();_Ui->objectActiveCheck->setChecked(false);
    for(auto* section:_Sections) delete section;
    _Sections.clear();
    SetDebugMode(_DebugMode);
    _Loading=false;
}
void GameObjectInspector::Load(GameSproutRuntime::IGameObject& object) {
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
    _Ui->objectActiveCheck->setVisible(!_SceneRoot);
    _Ui->objectActiveCheck->setChecked(object.IsActiveSelf());
    while(_Sections.size()>object.GetComponentCount()) {delete _Sections.back();_Sections.pop_back();}
    for(std::size_t index=0;index<object.GetComponentCount();++index) {
        auto* component=object.GetComponentAt(index);
        if(!component) continue;
        if(index==_Sections.size()) {
            auto* section=new ComponentInfoSection(_Ui->componentHost);
            _Ui->componentLayout->insertWidget(_Ui->componentLayout->count()-1,section);
            _Sections.push_back(section);
        }
        _Sections[index]->Bind(_Context);
        _Sections[index]->Load(*component);
        _Sections[index]->SetDebugMode(_DebugMode);
        _Sections[index]->setVisible(!_SceneRoot || _DebugMode);
    }
}

void GameObjectInspector::CommitName() {
    if (_Loading) return;
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
    if (_Loading) return;
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
