#include "GameObjectInspector.h"
#include "ComponentInfoSection.h"
#include "ui_GameObjectInspector.h"
#include <QApplication>

GameObjectInspector::GameObjectInspector(QWidget* parent) : QWidget(parent), _Ui(new Ui::GameObjectInspectorForm()) {
    _Ui->setupUi(this);
    QObject::connect(_Ui->objectNameEdit,&QLineEdit::editingFinished,this,[this]{CommitName();});
    QObject::connect(_Ui->objectActiveCheck,&QCheckBox::clicked,this,[this](bool active){CommitActive(active);});
}
GameObjectInspector::~GameObjectInspector() { delete _Ui; }
void GameObjectInspector::Clear() {
    _Loading=true;_Context={};_LoadedID=0;setEnabled(false);
    _Ui->objectNameEdit->clear();_Ui->objectInstanceIDEdit->clear();_Ui->sceneInstanceIDEdit->clear();
    _Ui->parentNameEdit->clear();_Ui->objectActiveCheck->setChecked(false);_Ui->hierarchyActiveEdit->clear();
    _Ui->componentCountLabel->setText(QStringLiteral("组件：0"));
    for(auto* section:_Sections) delete section;
    _Sections.clear();
    _Loading=false;
}
void GameObjectInspector::Load(GameSproutRuntime::IGameObject& object) {
    _Loading=true;setEnabled(true);
    struct Guard { bool& Value;~Guard(){Value=false;} } guard{_Loading};
    if (_LoadedID!=object.GetInstanceID() || QApplication::focusWidget()!=_Ui->objectNameEdit || !_Ui->objectNameEdit->isModified())
        _Ui->objectNameEdit->setText(QString::fromStdString(object.GetName()));
    _LoadedID=object.GetInstanceID();
    _Ui->objectInstanceIDEdit->setText(QString::number(object.GetInstanceID()));
    _Ui->sceneInstanceIDEdit->setText(object.GetScene()?QString::number(object.GetScene()->GetInstanceID()):QStringLiteral("—"));
    _Ui->parentNameEdit->setText(object.Parent()?QString::fromStdString(object.Parent()->GetName()):QStringLiteral("场景根"));
    _Ui->objectActiveCheck->setChecked(object.IsActiveSelf());
    _Ui->hierarchyActiveEdit->setText(object.IsActiveInHierarchy()?QStringLiteral("是"):QStringLiteral("否"));
    _Ui->componentCountLabel->setText(QStringLiteral("组件：%1").arg(object.GetComponentCount()));
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
