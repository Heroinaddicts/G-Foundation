#include "PrefabInstanceSection.h"
#include "IPrefabStageUI.h"
#include "ui_PrefabInstanceSection.h"
#include "ui_PrefabOverridesPopup.h"
#include <QDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QPushButton>
#include <QResizeEvent>

namespace {
QString Kind(PrefabOverrideKind kind) {
    switch(kind) {
    case PrefabOverrideKind::Property:return QStringLiteral("属性");
    case PrefabOverrideKind::AddedObject:return QStringLiteral("新增对象");
    case PrefabOverrideKind::RemovedObject:return QStringLiteral("删除对象");
    case PrefabOverrideKind::AddedComponent:return QStringLiteral("新增组件");
    case PrefabOverrideKind::RemovedComponent:return QStringLiteral("删除组件");
    }
    return {};
}
}
PrefabInstanceSection::PrefabInstanceSection(const InspectorServices& services,QWidget* parent)
    :InspectorSection(services,parent),_Ui(new Ui::PrefabInstanceSectionForm()),_PopupUi(new Ui::PrefabOverridesPopup()) {
    _Ui->setupUi(this);
    _Ui->sourceLabel->installEventFilter(this);
    _Popup=new QDialog(this,Qt::Popup);_PopupUi->setupUi(_Popup);
    _PopupUi->overridesTree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    _PopupUi->overridesTree->header()->setStretchLastSection(true);
    _Prefabs=services.Editor?services.Editor->GetGameSproutModule<IPrefabManager>("PrefabManager"):nullptr;
    _Play=services.Editor?services.Editor->GetGameSproutModule<IPlayMode>("PlayMode"):nullptr;
    if(_Prefabs)_PrefabSubscribed=_Prefabs->RegisterPrefabListener(this);
    if(services.Assets)_AssetSubscribed=services.Assets->RegisterAssetListener(this);
    if(services.Scenes)_SceneSubscribed=services.Scenes->RegisterSceneListener(this);
    if(_Play)_PlaySubscribed=_Play->RegisterPlayModeListener(this);
    QObject::connect(_Ui->overridesButton,&QToolButton::clicked,this,[this]{OpenOverrides();});
    QObject::connect(_Ui->openSourceButton,&QToolButton::clicked,this,[this]{OpenSource();});
    QObject::connect(_Ui->selectSourceButton,&QToolButton::clicked,this,[this]{SelectSource();});
    QObject::connect(_PopupUi->applyAllButton,&QPushButton::clicked,this,[this]{ApplyAll();});
    QObject::connect(_PopupUi->revertAllButton,&QPushButton::clicked,this,[this]{RevertAll();});
}
PrefabInstanceSection::~PrefabInstanceSection() {
    if(_PrefabSubscribed)_Prefabs->UnregisterPrefabListener(this);
    if(_AssetSubscribed)_Services.Assets->UnregisterAssetListener(this);
    if(_SceneSubscribed)_Services.Scenes->UnregisterSceneListener(this);
    if(_PlaySubscribed)_Play->UnregisterPlayModeListener(this);
    delete _Popup;delete _PopupUi;delete _Ui;
}
bool PrefabInstanceSection::Load(const InspectorTarget& target,bool changed,std::string& error) {
    if(changed)_Popup->hide();
    _Target=target;return Refresh(error);
}
void PrefabInstanceSection::Deactivate(){_Target={};_Popup->hide();}
bool PrefabInstanceSection::CanAuthor() const {
    return _Services.Scenes&&!_Services.Scenes->IsPlaySessionActive()
        &&(!_Play||_Play->GetState().State==EditorPlayState::Edit);
}
bool PrefabInstanceSection::Read(PrefabInstanceInfo& info,std::string& error) const {
    auto* scene=_Services.Scenes?_Services.Scenes->GetActiveScene():nullptr;
    if(!_Prefabs||!scene||_Target.Scene.SceneID!=scene->GetInstanceID()||!_Target.Scene.ObjectID) {
        error="检查的对象已删除或场景已切换。";return false;
    }
    return _Prefabs->ReadInstance(_Target.Scene.ObjectID,info,error);
}
bool PrefabInstanceSection::Refresh(std::string& error) {
    error.clear();PrefabInstanceInfo info;std::string readError;
    if(!Read(info,readError)){_Popup->hide();return false;}
    const QString path=QString::fromStdString(info.SourcePath);
    _SourceName=path.isEmpty()?QStringLiteral("来源不可用"):QFileInfo(path).fileName();UpdateSourceName();
    _Ui->sourceLabel->setToolTip(path);
    _Ui->selectSourceButton->setEnabled(!info.MissingSource&&!info.Source.Guid.empty());
    _Ui->openSourceButton->setEnabled(!info.MissingSource&&CanAuthor()&&_Services.Editor&&_Services.Editor->GetGameSproutView<IPrefabStageUI>("PrefabStageUI"));
    const bool root=_Target.Scene.ObjectID==info.RootObjectID;
    _Ui->overridesButton->setVisible(root);
    _Ui->overridesButton->setText(info.HasOverrides?QStringLiteral("Overrides (%1) ▾").arg(info.Overrides.size()):QStringLiteral("Overrides ▾"));
    const bool enabled=root&&CanAuthor()&&info.HasOverrides&&!info.MissingSource&&info.Error.empty();
    _PopupUi->applyAllButton->setEnabled(enabled);_PopupUi->revertAllButton->setEnabled(enabled);
    QString state;
    if(info.MissingSource)state=QStringLiteral("MissingSource：来源缺失，保留实例连接。%1").arg(QString::fromStdString(info.Error));
    else if(!info.Error.empty())state=QString::fromStdString(info.Error);
    else if(!CanAuthor())state=QStringLiteral("播放期间不能应用或恢复覆盖。");
    _Ui->stateLabel->setText(state);_Ui->stateLabel->setVisible(!state.isEmpty());
    _PopupUi->messageLabel->setText(state.isEmpty()&&!info.HasOverrides?QStringLiteral("实例与来源一致。"):state);
    _PopupUi->overridesTree->clear();
    for(const auto& item:info.Overrides) {
        const QString values=QString::fromStdString(item.SourceValueJson)+QStringLiteral(" → ")+QString::fromStdString(item.InstanceValueJson);
        const QString object=QString::fromStdString(item.ObjectName.empty()?"对象":item.ObjectName);
        const QString component=QString::fromStdString(item.ComponentType);
        auto* row=new QTreeWidgetItem(_PopupUi->overridesTree,{Kind(item.Kind),component.isEmpty()?object:object+" / "+component,
            QString::fromStdString(item.PropertyPath),values});
        row->setToolTip(1,QStringLiteral("SourceObjectID: %1\nComponentID: %2").arg(item.SourceObjectID).arg(item.ComponentID));row->setToolTip(3,values);
    }
    return true;
}
void PrefabInstanceSection::QueueRefresh(){
    if(_Queued)return;_Queued=true;
    QMetaObject::invokeMethod(this,[this]{_Queued=false;if(RequestRefresh)RequestRefresh();else {std::string ignored;setVisible(Refresh(ignored));}},Qt::QueuedConnection);
}
void PrefabInstanceSection::UpdateSourceName(){_Ui->sourceLabel->setText(_Ui->sourceLabel->fontMetrics().elidedText(_SourceName,Qt::ElideRight,_Ui->sourceLabel->width()));}
void PrefabInstanceSection::resizeEvent(QResizeEvent* event){InspectorSection::resizeEvent(event);UpdateSourceName();}
bool PrefabInstanceSection::eventFilter(QObject* watched,QEvent* event){
    if(watched==_Ui->sourceLabel&&event->type()==QEvent::Resize)UpdateSourceName();
    return InspectorSection::eventFilter(watched,event);
}
void PrefabInstanceSection::Error(const std::string& error){
    _Ui->stateLabel->setText(QString::fromStdString(error));_Ui->stateLabel->show();_PopupUi->messageLabel->setText(QString::fromStdString(error));
    if(ReportError)ReportError(error);
}
void PrefabInstanceSection::OpenOverrides(){
    std::string error;PrefabInstanceInfo info;
    if(!Read(info,error)||_Target.Scene.ObjectID!=info.RootObjectID)return;
    Refresh(error);_Popup->move(_Ui->overridesButton->mapToGlobal(QPoint(0,_Ui->overridesButton->height())));_Popup->show();
}
void PrefabInstanceSection::ApplyAll(){
    if(!CanAuthor()){Error("播放期间不能 Apply Prefab。");return;}
    PrefabInstanceInfo info;std::string error;
    if(!Read(info,error)){Error(error);return;}
    if(_Target.Scene.ObjectID!=info.RootObjectID){Error("请检查实例根以应用全部覆盖。");return;}
    if(info.MissingSource||!info.Error.empty()){Error(info.Error.empty()?"Prefab 来源缺失，不能 Apply。":info.Error);return;}
    if(info.HasOverrides&&!_Prefabs->ApplyOverrides(info.RootObjectID,error)){Error(error);return;}QueueRefresh();
}
void PrefabInstanceSection::RevertAll(){
    if(!CanAuthor()){Error("播放期间不能 Revert Prefab。");return;}
    PrefabInstanceInfo info;std::string error;
    if(!Read(info,error)){Error(error);return;}
    if(_Target.Scene.ObjectID!=info.RootObjectID){Error("请检查实例根以恢复全部覆盖。");return;}
    if(info.MissingSource||!info.Error.empty()){Error(info.Error.empty()?"Prefab 来源缺失，不能 Revert。":info.Error);return;}
    if(info.HasOverrides&&!_Prefabs->RevertOverrides(info.RootObjectID,error)){Error(error);return;}QueueRefresh();
}
void PrefabInstanceSection::OpenSource(){
    PrefabInstanceInfo info;std::string error;if(!CanAuthor()||!Read(info,error)||info.MissingSource||!_Services.Editor)return;
    if(auto* stage=_Services.Editor->GetGameSproutView<IPrefabStageUI>("PrefabStageUI"))stage->OpenPrefab(info.Source.Guid);
}
void PrefabInstanceSection::SelectSource(){
    PrefabInstanceInfo info;std::string error;if(!Read(info,error)||info.MissingSource)return;
    if(RequestSelection)RequestSelection({info.Source.Guid,0,false});
}
void PrefabInstanceSection::OnPrefabStateChanged(){QueueRefresh();}
void PrefabInstanceSection::OnAssetCreate(const AssetEvent&){QueueRefresh();}
void PrefabInstanceSection::OnAssetModify(const AssetEvent&){QueueRefresh();}
void PrefabInstanceSection::OnAssetRemove(const AssetEvent&){QueueRefresh();}
void PrefabInstanceSection::OnActiveSceneChanged(){_Popup->hide();QueueRefresh();}
void PrefabInstanceSection::OnSceneStateChanged(){QueueRefresh();}
void PrefabInstanceSection::OnPlayModeStateChanged(const PlayModeState&){_Popup->hide();QueueRefresh();}
