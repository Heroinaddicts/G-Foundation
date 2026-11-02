#include "PrefabStageUI.h"
#include "IAssetsManager.h"
#include "IIDEWindow.h"
#include "IHierarchyUI.h"
#include "ui_PrefabStageToolbar.h"
#include "ui_PrefabStageHierarchyHeader.h"
#include <QApplication>
#include <QDialog>
#include <QFileInfo>
#include <QToolButton>
#include <QEvent>

namespace {
class StageLabelResize final : public QObject {
public:
    StageLabelResize(QObject* owner,std::function<void()> callback):QObject(owner),_Callback(std::move(callback)){}
    bool eventFilter(QObject*,QEvent* event) override {if(event->type()==QEvent::Resize&&_Callback)_Callback();return false;}
private:
    std::function<void()> _Callback;
};
}

PrefabStageUI::~PrefabStageUI(){Release(nullptr);}
void PrefabStageUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* api) {
    g_Self=this;g_GameSproutEditor=api;if(!api)return;
    GetGameSproutView(api,_IDE,IDEWindow);GetGameSproutView(api,_Project,ProjectAssetsUI);
    GetGameSproutView(api,_SceneEditor,SceneEditorUI);GetGameSproutView(api,_SceneView,SceneViewUI);
    GetGameSproutView(api,_Inspector,InspectorUI);
    GetGameSproutView(api,_Hierarchy,HierarchyUI);
    GetGameSproutModule(api,_Stage,PrefabStage);GetGameSproutModule(api,_Assets,AssetsManager);
    GetGameSproutModule(api,_Play,PlayMode);
}
bool PrefabStageUI::Initialize(GameSproutEditor::GameSproutViewApi*) {
    if(_Toolbar)return true;
    if(!qobject_cast<QApplication*>(QCoreApplication::instance()))return false;
    _Dispatch=new QObject();_Toolbar=new QWidget();_Ui=new Ui::PrefabStageToolbarForm();_Ui->setupUi(_Toolbar);
    _Ui->sourceLabel->installEventFilter(new StageLabelResize(_Dispatch,[this]{UpdateSourceName();}));
    QObject::connect(_Toolbar,&QObject::destroyed,_Dispatch,[this]{_Toolbar=nullptr;_Attached=false;});
    QObject::connect(_Ui->returnButton,&QToolButton::clicked,_Dispatch,[this]{ReturnToMainScene();});
    QObject::connect(_Ui->saveButton,&QToolButton::clicked,_Dispatch,[this]{Save();});
    _HierarchyHeader=new QWidget();_HeaderUi=new Ui::PrefabStageHierarchyHeaderForm();_HeaderUi->setupUi(_HierarchyHeader);
    _HeaderUi->sourceLabel->installEventFilter(new StageLabelResize(_Dispatch,[this]{UpdateSourceName();}));
    QObject::connect(_HierarchyHeader,&QObject::destroyed,_Dispatch,[this]{_HierarchyHeader=nullptr;_HeaderAttached=false;});
    QObject::connect(_HeaderUi->returnButton,&QToolButton::clicked,_Dispatch,[this]{ReturnToMainScene();});
    _HierarchyHeader->hide();
    _Toolbar->hide();return true;
}
bool PrefabStageUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    if(!_Toolbar||!_IDE||!_Project||!_SceneEditor||!_Stage||!_Assets)return false;
    if(!_IDE->AttachToolbarWidget(_Toolbar))return false;_Attached=true;
    if(_Hierarchy){if(!_Hierarchy->AttachHeaderWidget(_HierarchyHeader))return false;_HeaderAttached=true;}
    _StageSubscribed=_Stage->RegisterStageListener(this);if(!_StageSubscribed)return false;
    if(_Play){_PlaySubscribed=_Play->RegisterPlayModeListener(this);if(!_PlaySubscribed)return false;_CanEdit=_Play->GetState().State==EditorPlayState::Edit;}
    if(!_Project->RegisterAssetOpenHandler(this,[this](const ProjectAssetSelection& asset){return OpenAsset(asset);}))return false;
    _CommandsRegistered=_SceneEditor->RegisterDocumentHandler(this,[this]{return _Stage&&_Stage->GetState().Active;},
        [this]{Save();},[this](const std::function<void()>& continuation){RequestClose(continuation);});
    if(!_CommandsRegistered)return false;
    Refresh(_Stage->GetState());return true;
}
void PrefabStageUI::Release(GameSproutEditor::GameSproutViewApi*) {
    ++_NavigationGeneration;
    if(_Project)_Project->UnregisterExtensions(this);
    if(_CommandsRegistered&&_SceneEditor)_SceneEditor->UnregisterDocumentHandler(this);
    if(_StageSubscribed&&_Stage)_Stage->UnregisterStageListener(this);
    if(_PlaySubscribed&&_Play)_Play->UnregisterPlayModeListener(this);
    _CommandsRegistered=_StageSubscribed=_PlaySubscribed=false;
    delete _CloseDialog.data();_CloseDialog=nullptr;delete _ErrorDialog.data();_ErrorDialog=nullptr;
    delete _Dispatch;_Dispatch=nullptr;
    if(_HierarchyHeader) {
        if(_HeaderAttached&&_Hierarchy)_Hierarchy->RemoveHeaderWidget(_HierarchyHeader);
        else delete _HierarchyHeader.data();
    }
    _HierarchyHeader=nullptr;_HeaderAttached=false;delete _HeaderUi;_HeaderUi=nullptr;
    if(_Toolbar) {
        if(_Attached&&_IDE)_IDE->RemoveToolbarWidget(_Toolbar);
        else delete _Toolbar.data();
    }
    _Toolbar=nullptr;_Attached=false;delete _Ui;_Ui=nullptr;
    _Stage=nullptr;_IDE=nullptr;_Assets=nullptr;_Project=nullptr;_SceneEditor=nullptr;_SceneView=nullptr;_Play=nullptr;_Inspector=nullptr;_Hierarchy=nullptr;
    _StageActive=false;_HaveMainCamera=false;_StageSceneID=0;_DialogGuid.clear();_HaveMainInspector=false;_MainInspector={};
    if(g_Self==this)g_Self=nullptr;g_GameSproutEditor=nullptr;
}
bool PrefabStageUI::OpenAsset(const ProjectAssetSelection& asset) {
    if(asset.IsSubAsset||!_Assets)return false;
    const auto* meta=_Assets->QueryAsset(Guid(asset.GuidString));
    if(!meta||meta->_ImporterType!=eAssetImporterType::Prefab)return false;
    OpenPrefab(asset.GuidString);return true;
}
void PrefabStageUI::OpenPrefab(const std::string& guid) {
    QueueNavigation([this,guid]{OpenRequested(guid);});
}
void PrefabStageUI::QueueNavigation(const std::function<void()>& operation) {
    if(!_Dispatch||!operation)return;
    const uint64_t generation=++_NavigationGeneration;
    // Run once after the click/draw callback has returned. QObject ownership
    // cancels delivery after release; the generation cancels superseded work.
    QMetaObject::invokeMethod(_Dispatch,[this,generation,operation]{
        if(generation==_NavigationGeneration)operation();
    },Qt::QueuedConnection);
}
void PrefabStageUI::OpenRequested(const std::string& guid) {
    if(!_Stage||!_CanEdit)return;
    if(QWidget* focused=QApplication::focusWidget())focused->clearFocus();
    const auto state=_Stage->GetState();
    if(state.Active&&state.Guid==guid){if(_SceneView)_SceneView->Show();return;}
    if(state.Active){CloseNow([this,guid]{OpenNow(guid);});return;}
    OpenNow(guid);
}
void PrefabStageUI::OpenNow(const std::string& guid) {
    if(!_Stage||!_CanEdit)return;
    // Capture before Module switches worlds; the Inspector may be locked to
    // a main-scene object that is deliberately not globally selected.
    const bool captureInspector=_Inspector&&!_Stage->GetState().Active&&!_HaveMainInspector;
    if(captureInspector){_MainInspector=_Inspector->CaptureViewState();_HaveMainInspector=true;}
    std::string error;if(!_Stage->OpenPrefab(guid,error)){
        if(captureInspector)_HaveMainInspector=false;
        ShowError(error);return;
    }
    if(_SceneView)_SceneView->Show();
}
void PrefabStageUI::Save() {
    QueueNavigation([this]{SaveNow();});
}
void PrefabStageUI::SaveNow() {
    if(!_Stage||!_CanEdit)return;
    if(QWidget* focused=QApplication::focusWidget())focused->clearFocus();
    std::string error;if(!_Stage->Save(error))ShowError(error);
    Refresh(_Stage->GetState());
}
void PrefabStageUI::ReturnToMainScene(){RequestClose();}
void PrefabStageUI::OnPrefabStageChanged(const PrefabStageState& state){Refresh(state);}
void PrefabStageUI::OnPlayModeStateChanged(const PlayModeState& state) {
    _CanEdit=state.State==EditorPlayState::Edit;
    if(!_CanEdit&&_CloseDialog)_CloseDialog->reject();
    if(_Stage)Refresh(_Stage->GetState());
}
void PrefabStageUI::Refresh(const PrefabStageState& state) {
    if(!_Toolbar||!_Ui)return;
    if(state.Active&&!_StageActive) {
        if(_SceneView){_MainCamera=_SceneView->GetCameraState();_HaveMainCamera=true;}
        if(_Inspector) {
            if(!_HaveMainInspector){_MainInspector=_Inspector->CaptureViewState();_HaveMainInspector=true;}
            InspectorViewState stageInspector;stageInspector.Scene={state.SceneID,state.ContentRootObjectID};
            _Inspector->RestoreViewState(stageInspector);
        }
    }
    if(state.Active&&_StageSceneID!=state.SceneID&&_SceneView)_SceneView->FocusSelection();
    if(!state.Active&&_StageActive&&_HaveMainCamera&&_SceneView){_SceneView->SetCameraState(_MainCamera);_HaveMainCamera=false;}
    if(!state.Active&&_StageActive&&_HaveMainInspector) {
        if(_Inspector)_Inspector->RestoreViewState(_MainInspector);
        _HaveMainInspector=false;_MainInspector={};
    }
    _StageActive=state.Active;_StageSceneID=state.SceneID;
    if(_Attached&&_IDE)_IDE->SetToolbarWidgetVisible(_Toolbar,state.Active);
    else _Toolbar->setVisible(state.Active);
    _SourceCaption=QFileInfo(QString::fromStdString(state.Path)).fileName()+(state.Dirty?QStringLiteral(" *"):QString());UpdateSourceName();
    _Ui->sourceLabel->setToolTip(QString::fromStdString(state.Path));
    _Ui->stateLabel->setText(state.MissingSource?QStringLiteral("来源缺失"):state.ExternalConflict?QStringLiteral("源文件已改变"):QStringLiteral("操作提示"));
    _Ui->stateLabel->setToolTip(QString::fromStdString(state.Error));
    _Ui->stateLabel->setVisible(!state.Error.empty());
    _Ui->saveButton->setEnabled(state.Active&&_CanEdit&&!state.MissingSource&&!state.ExternalConflict);
    _Ui->returnButton->setEnabled(state.Active&&_CanEdit);
    if(_HierarchyHeader&&_HeaderUi) {
        if(_HeaderAttached&&_Hierarchy)_Hierarchy->SetHeaderWidgetVisible(_HierarchyHeader,state.Active);
        else _HierarchyHeader->hide();
        _HeaderUi->sourceLabel->setToolTip(QString::fromStdString(state.Path));
        _HeaderUi->returnButton->setEnabled(state.Active&&_CanEdit);
    }
    if(_CloseDialog&&(!state.Active||state.Guid!=_DialogGuid))_CloseDialog->reject();
    if(_SceneEditor)_SceneEditor->RefreshDocumentActions();
}

void PrefabStageUI::UpdateSourceName(){
    if(_Ui&&_Toolbar)_Ui->sourceLabel->setText(_Ui->sourceLabel->fontMetrics().elidedText(_SourceCaption,Qt::ElideRight,_Ui->sourceLabel->width()));
    if(_HeaderUi&&_HierarchyHeader)_HeaderUi->sourceLabel->setText(_HeaderUi->sourceLabel->fontMetrics().elidedText(_SourceCaption,Qt::ElideRight,_HeaderUi->sourceLabel->width()));
}
