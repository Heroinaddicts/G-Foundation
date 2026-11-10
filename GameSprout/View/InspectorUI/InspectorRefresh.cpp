#include "InspectorUI.h"
#include "ui_InspectorUI.h"
#include <QLayout>
#include <utility>
void InspectorUI::Clear(const QString& message) {
    ObserveObject(0);
    DeactivateSceneSections();
    if (_ActivePanel) { _ActivePanel->Deactivate(); _ActivePanel->hide(); }
    _ActivePanel = nullptr; _ActiveExtension = nullptr;
    _OperationError.clear(); _Ui->assetNameLabel->setText("Inspector"); _Ui->assetNameLabel->setToolTip({});
    _Ui->typeLabel->clear(); _Ui->stateLabel->clear(); _Ui->pathEdit->clear(); _Ui->guidEdit->clear(); _Ui->localIDEdit->clear();
    _Ui->emptyLabel->setText(message); _Ui->contentStack->setCurrentWidget(_Ui->emptyPage); UpdateButtons();
}
bool InspectorUI::SceneTargetExists(const SceneObjectSelection& target) const {
    auto* scene = _Services.Scenes ? _Services.Scenes->GetActiveScene() : nullptr;
    auto* object = _Runtime ? _Runtime->QueryGameObject(target.ObjectID) : nullptr;
    return scene && scene->GetInstanceID() == target.SceneID && object && object->GetScene() == scene;
}
void InspectorUI::RefreshPending() {
    const bool full=std::exchange(_FullRefreshRequested,false);
    auto sections=std::exchange(_SectionsToRefresh,std::set<IInspectorExtension*>{});
    const bool observed=std::exchange(_ObservedPropertiesPending,false);
    const auto changes=std::exchange(_ObjectChanges,InspectorObjectChanges{});
    if(full||!_ActivePanel||!_Target.Scene.ObjectID||!SceneTargetExists(_Target.Scene)) {
        Refresh();
        return;
    }
    if(observed||!changes.Empty()) {
        const bool refreshWholePanel=changes.Empty()||
            (observed&&_Services.Scenes&&_Services.Scenes->IsPlaySessionActive());
        if(refreshWholePanel||!_ActivePanel->RefreshObjectProperties(changes)) {
            std::string error;
            if(!_ActivePanel->Load(_Target,false,error)){Clear(QString::fromStdString(error));return;}
            _OperationError=QString::fromStdString(error);
        }
    }
    if(!sections.empty())RefreshSceneSections(_Target,false,&sections);
    UpdateButtons();
}
void InspectorUI::Refresh() {
    if (!_Window || _ResolvingSelection || !_Services.Selection) return;
    if (_Ui->lockButton->isChecked() && _LockedSceneMissing) { Clear(QStringLiteral("锁定的节点已删除或场景已切换。解锁后可查看其他选择。")); return; }
    InspectorTarget desired;
    desired.Assets = _Services.Selection->GetAssetSelection(); desired.Scene = _Services.Selection->GetSceneObjectSelection();
    if (_Ui->lockButton->isChecked()) desired = _Target;
    if (desired.Scene.ObjectID) desired.Assets.clear();
    const bool changed = desired.Assets != _Target.Assets || desired.Scene != _Target.Scene;
    const bool previousExists = changed && (_Target.Scene.ObjectID ? SceneTargetExists(_Target.Scene)
        : (_Target.Assets.size() == 1 && _Services.Assets->QueryAsset(Guid(_Target.Assets.front().GuidString))));
    if (previousExists && !ResolvePendingChanges()) {
        if (_Target.Scene.ObjectID) _Services.Selection->SetSceneObjectSelection(_Target.Scene);
        else _Services.Selection->SetAssetSelection(_Target.Assets);
        return;
    }
    if (desired.Scene.ObjectID && !SceneTargetExists(desired.Scene)) {
        _LockedSceneMissing = _Ui->lockButton->isChecked(); _Target = {};
        Clear(_LockedSceneMissing ? QStringLiteral("锁定的节点已删除或场景已切换。解锁后可查看其他选择。") : QStringLiteral("节点已删除或场景已切换。")); return;
    }
    if (!desired.Scene.ObjectID && desired.Assets.size() != 1) {
        _Target = desired; Clear(desired.Assets.empty() ? QStringLiteral("选择一个资源或场景节点以查看属性。") : QStringLiteral("已选择 %1 项。当前版本请单选资源查看与编辑属性。").arg(desired.Assets.size())); return;
    }
    IInspectorExtension* selected = nullptr;
    for (auto* extension : _Extensions) if (extension->CanInspect(_Services, desired)) { selected = extension; break; }
    if (!selected) { _Target = desired; Clear(QStringLiteral("此目标尚未注册属性面板。")); return; }
    if (_ActiveExtension != selected && _ActivePanel) { _ActivePanel->Deactivate(); _ActivePanel->hide(); }
    auto found = _Panels.find(selected);
    if (found == _Panels.end()) {
        QWidget* parent = desired.Scene.ObjectID ? _Ui->sceneHost : _Ui->textureHost;
        auto* panel = selected->CreatePanel(_Services, parent);
        if (!panel) { Clear(QStringLiteral("无法创建属性面板。")); return; }
        (desired.Scene.ObjectID ? _Ui->sceneHostLayout : _Ui->textureHostLayout)->addWidget(panel);
        panel->Changed = [this] { _OperationError.clear(); UpdateButtons(); };
        panel->ReportError = [this](const std::string& message) { _OperationError = QString::fromStdString(message); UpdateButtons(); };
        panel->RequestSelection = [this](const ProjectAssetSelection& target) { _Ui->lockButton->setChecked(false); _Services.Selection->SetAssetSelection({target}); };
        panel->RequestLock = [this](bool locked) { _Ui->lockButton->setChecked(locked); };
        panel->ResolvePendingChanges = [this] { return ResolvePendingChanges(); };
        found = _Panels.emplace(selected, panel).first;
    }
    ObserveObject(desired.Scene.ObjectID);
    _Target = desired; _ActivePanel = found->second; _ActiveExtension = selected;
    std::string error;
    if (!_ActivePanel->Load(desired, changed, error)) { Clear(QString::fromStdString(error)); return; }
    _OperationError = QString::fromStdString(error);
    for (auto& entry : _Panels) entry.second->setVisible(entry.second == _ActivePanel);
    RefreshSceneSections(desired,changed);
    _Ui->textureHost->show(); _Ui->contentStack->setCurrentWidget(desired.Scene.ObjectID ? _Ui->scenePage : _Ui->propertiesPage);
    UpdateButtons();
}

void InspectorUI::DeactivateSceneSections() {
    for(auto& entry:_SceneSections)if(auto* section=entry.second.Section.data()){section->Deactivate();section->hide();}
    if(_ActivePanel)if(auto* header=_ActivePanel->SceneObjectHeaderHost())header->hide();
    if(_Window)_Ui->sceneSectionsHost->hide();
}
void InspectorUI::RefreshSceneSections(const InspectorTarget& target,bool changed,const std::set<IInspectorExtension*>* only) {
    if(!target.Scene.ObjectID){DeactivateSceneSections();return;}
    QWidget* header=_ActivePanel?_ActivePanel->SceneObjectHeaderHost():nullptr;
    bool bodyVisible=false,headerVisible=false;
    for(auto* extension:_Extensions) {
        const bool inHeader=extension->SceneObjectSectionPlacement()==InspectorSectionPlacement::ObjectHeader;
        QWidget* parent=inHeader?header:_Ui->sceneSectionsHost;
        auto& entry=_SceneSections[extension];
        if(!parent||!parent->layout()) {
            if(entry.Section){entry.Section->Deactivate();entry.Section->hide();}
            continue;
        }
        // Header hosts belong to the selected functional panel. QPointer prevents
        // stale entries if that panel is unregistered or the window is destroyed.
        if(entry.Host!=parent) {
            if(entry.Section){entry.Section->Deactivate();delete entry.Section.data();}
            entry.Host=parent;entry.Section=extension->CreateSceneObjectSection(_Services,parent);
            if(auto* section=entry.Section.data()) {
                parent->layout()->addWidget(section);
                section->ReportError=[this](const std::string& error){_OperationError=QString::fromStdString(error);UpdateButtons();};
                section->RequestRefresh=[this,extension]{_SectionsToRefresh.insert(extension);QueueRefresh();};
                section->RequestSelection=[this](const ProjectAssetSelection& asset){_Services.Selection->SetAssetSelection({asset});};
            }
        }
        if(auto* section=entry.Section.data()) {
            bool applicable=!section->isHidden();
            if(!only||only->contains(extension)) {
                std::string error;
                applicable=section->Load(target,changed,error);
                section->setVisible(applicable);
                if(!error.empty())_OperationError=QString::fromStdString(error);
            }
            if(inHeader)headerVisible=headerVisible||applicable;else bodyVisible=bodyVisible||applicable;
        }
    }
    if(header)header->setVisible(headerVisible);
    _Ui->sceneSectionsHost->setVisible(bodyVisible);
}
