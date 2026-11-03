#include "SceneViewUI.h"
#include "SceneViewport.h"
#include "SceneViewportEditing.h"
#include "SceneToolRouter.h"
#include "SceneViewChrome.h"
#include "ISceneManager.h"
#include "ISelectionManager.h"
#include "IRuntimeAssets.h"
#include "ui_SceneViewUI.h"
#include <QApplication>
#include <algorithm>
#include <cmath>
#include <limits>

SceneViewUI::~SceneViewUI(){Release(nullptr);}
void SceneViewUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* editor) {
    g_Self=this;g_GameSproutEditor=editor;
    if(!editor)return;
    _PlayMode=editor->GetGameSproutModule<IPlayMode>("PlayMode");
    GetGameSproutView(editor,g_IIDEWindow,IDEWindow);
    GetGameSproutModule(editor,_Scenes,SceneManager);
    GetGameSproutModule(editor,_Host,RuntimeHost);
    _Runtime=editor?editor->_Runtime:nullptr;
    GetGameSproutModule(editor,_Selection,SelectionManager);
    GetGameSproutModule(editor,_RuntimeAssets,RuntimeAssets);
}
bool SceneViewUI::Initialize(GameSproutEditor::GameSproutViewApi* editor) {
    if(_Window)return true;
    delete _Dispatch;_Dispatch=nullptr;delete _Ui;_Ui=nullptr;
    if(!qobject_cast<QApplication*>(QCoreApplication::instance()))return false;
    _Dispatch=new QObject();_Window=new QWidget();_Ui=new Ui::SceneViewUIWindow();_Ui->setupUi(_Window);
    _Viewport=_Ui->sceneViewport;
    _Chrome=new SceneViewChrome(_Window,*_Ui);
    _Viewport->StatusChanged=[this](const std::string& message){if(_Chrome&&_Viewport)_Chrome->SetStatus(message,_Viewport->GetRenderError());};
    _Viewport->FocusRequested=[this]{return FocusSelection();};
    _Viewport->Initialize(_Runtime,_RuntimeAssets?_RuntimeAssets->GetResourceProvider():nullptr);
    _Viewport->BindEditingServices(_Scenes,_Selection);
    _Viewport->Editing().ToolChanged=[this](SceneEditingTool tool){
        if(!_Ui||!_Window)return;
        QToolButton* button=tool==SceneEditingTool::View?_Ui->viewToolButton:tool==SceneEditingTool::Move?_Ui->moveToolButton:tool==SceneEditingTool::Rotate?_Ui->rotateToolButton:_Ui->scaleToolButton;
        button->setChecked(true);
    };
    QObject::connect(_Ui->viewToolButton,&QToolButton::clicked,_Dispatch,[this]{_Viewport->Editing().SetTool(SceneEditingTool::View);});
    QObject::connect(_Ui->moveToolButton,&QToolButton::clicked,_Dispatch,[this]{_Viewport->Editing().SetTool(SceneEditingTool::Move);});
    QObject::connect(_Ui->rotateToolButton,&QToolButton::clicked,_Dispatch,[this]{_Viewport->Editing().SetTool(SceneEditingTool::Rotate);});
    QObject::connect(_Ui->scaleToolButton,&QToolButton::clicked,_Dispatch,[this]{_Viewport->Editing().SetTool(SceneEditingTool::Scale);});
    QObject::connect(_Ui->spaceCombo,qOverload<int>(&QComboBox::currentIndexChanged),_Dispatch,[this](int index){_Viewport->Editing().SetSpace(index==0?SceneGizmoSpace::World:SceneGizmoSpace::Local);});
    QObject::connect(_Ui->pivotCombo,qOverload<int>(&QComboBox::currentIndexChanged),_Dispatch,[this](int index){_Viewport->Editing().SetPivot(index==0?SceneGizmoPivot::Pivot:SceneGizmoPivot::Center);});
    const auto snap=[this]{_Viewport->Editing().SetSnap(_Chrome->SnapEnabled(),_Chrome->MoveSnap(),_Chrome->RotateSnap(),_Chrome->ScaleSnap());_Viewport->Tools().SetSnap(_Chrome->SnapEnabled(),_Chrome->MoveSnap());};
    _Chrome->SnapChanged=snap;
    snap();
    QObject::connect(_Ui->resetCameraButton,&QToolButton::clicked,_Dispatch,[this]{ResetCamera();});
    QObject::connect(_Ui->focusSelectionButton,&QToolButton::clicked,_Dispatch,[this]{FocusSelection();});
    QObject::connect(_Ui->retryRenderButton,&QToolButton::clicked,_Dispatch,[this]{if(_Viewport)_Viewport->RetryRendering();});
    QObject::connect(_Ui->allCollidersCheck,&QCheckBox::toggled,_Dispatch,[this](bool checked){_Viewport->Editing().SetShowAllColliders(checked);});
    QObject::connect(_Ui->gridCheck,&QCheckBox::toggled,_Dispatch,[this](bool){if(_Viewport)_Viewport->RequestRender();});
    QObject::connect(_Window,&QObject::destroyed,_Dispatch,[this]{_Window=nullptr;_Viewport=nullptr;_Docker=nullptr;_Attached=false;});
    return true;
}
bool SceneViewUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    if(!_Window||!g_IIDEWindow)return false;
    _Docker=g_IIDEWindow->CreateDocker(QStringLiteral("Scene"));
    if(!_Docker||!_Docker->AttachWidget(_Window))return false;
    _Attached=true;_Docker->SetTitle(QStringLiteral("Scene"));
    if(!_Chrome->Attach(_Docker))return false;
    _DockerVisibilitySubscribed=_Docker->RegisterContentVisibilityCallback(this,
        [this](bool visible){_DockerContentVisible=visible;_Timer.start();if(visible&&_Viewport)_Viewport->RequestRender();});
    _DockerContentVisible=_Docker->IsContentVisible();
    _LoadingSubscribed=g_IIDEWindow->RegisterLoadingStateCallback(this,[this](bool loading){_Loading=loading;_Timer.start();if(!loading&&_Viewport)_Viewport->RequestRender();});
    _Loading=g_IIDEWindow->IsLoading();
    if(_Scenes)_SceneSubscribed=_Scenes->RegisterSceneListener(this);
    if(_Host)_FrameSubscribed=_Host->RegisterFrameListener(this);
    if(_RuntimeAssets)_ResourcesSubscribed=_RuntimeAssets->RegisterResourceListener(this);
    if(_PlayMode){_PlaySubscribed=_PlayMode->RegisterPlayModeListener(this);OnPlayModeStateChanged(_PlayMode->GetState());}
    OnActiveSceneChanged();_Timer.start();
    return _DockerVisibilitySubscribed && (!_Scenes || _SceneSubscribed) && _Host && _FrameSubscribed;
}
void SceneViewUI::LaunchFinished(GameSproutEditor::GameSproutViewApi*){}
void SceneViewUI::Show(){if(_Window)_Window->show();if(_Docker)_Docker->Activate();}
void SceneViewUI::Hide(){if(_Window)_Window->hide();}
void SceneViewUI::Update(GameSproutEditor::GameSproutViewApi*) {}
void SceneViewUI::OnActiveSceneChanged() {
    _ActiveScene=_Scenes?_Scenes->GetActiveScene():nullptr;
    if(_Viewport)_Viewport->RequestRender();
}
void SceneViewUI::OnRuntimeResourcesChanged(std::uint64_t){if(_Viewport)_Viewport->RequestRender();}
void SceneViewUI::OnRuntimeFrame(const RuntimeFrameEvent& event) {
    // Editor camera navigation continues while simulation is paused.
    const float elapsed=_Timer.isValid()?static_cast<float>(_Timer.nsecsElapsed()/1000000000.0):0.0f;_Timer.start();
    const bool continuous=event.SimulationPlaying||(event.GameplayActive&&(!event.GameplayPaused||event.DeltaSeconds>0));
    const bool animateMaterials=!event.GameplayActive||!event.GameplayPaused||event.DeltaSeconds>0;
    if(!_Loading && _DockerContentVisible && _Viewport && _Window && _Viewport->isVisible())_Viewport->RenderScene(_ActiveScene,_Ui->gridCheck->isChecked(),elapsed,continuous,animateMaterials);
}
void SceneViewUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if(_ResourcesSubscribed&&_RuntimeAssets)_RuntimeAssets->UnregisterResourceListener(this);
    _ResourcesSubscribed=false;
    if(_LoadingSubscribed && g_IIDEWindow)g_IIDEWindow->UnregisterLoadingStateCallback(this);
    _LoadingSubscribed=false;_Loading=false;
    if(_DockerVisibilitySubscribed && _Docker)_Docker->UnregisterContentVisibilityCallback(this);
    _DockerVisibilitySubscribed=false;_DockerContentVisible=false;
    if(_PlaySubscribed&&_PlayMode)_PlayMode->UnregisterPlayModeListener(this);
    _PlaySubscribed=false;_PlayMode=nullptr;_CanEdit=true;
    if(_FrameSubscribed && _Host)_Host->UnregisterFrameListener(this);
    if(_SceneSubscribed && _Scenes)_Scenes->UnregisterSceneListener(this);
    _FrameSubscribed=false;_SceneSubscribed=false;_ActiveScene=nullptr;_Host=nullptr;
    if(_Viewport){_Viewport->Editing().ToolChanged={};_Viewport->FocusRequested={};_Viewport->StatusChanged={};_Viewport->Shutdown();}
    delete _Chrome;_Chrome=nullptr;
    delete _Dispatch;_Dispatch=nullptr;
    if(!_Attached)delete _Window;
    _Window=nullptr;_Viewport=nullptr;_Docker=nullptr;_Attached=false;
    delete _Ui;_Ui=nullptr;_Scenes=nullptr;_Runtime=nullptr;_Selection=nullptr;_RuntimeAssets=nullptr;_Timer.invalidate();
    if(g_Self==this)g_Self=nullptr;
}
SceneViewCameraState SceneViewUI::GetCameraState() const{return _Viewport?_Viewport->Camera().GetState():SceneViewCameraState{};}
bool SceneViewUI::SetCameraState(const SceneViewCameraState& state){if(!_Viewport)return false;_Viewport->Editing().Cancel();_Viewport->Tools().CancelDrag();return _Viewport->Camera().SetState(state);}
void SceneViewUI::ResetCamera(){if(_Viewport){_Viewport->Editing().Cancel();_Viewport->Tools().CancelDrag();_Viewport->Camera().Reset();}}
bool SceneViewUI::IsRenderingAvailable() const{return _Viewport&&_Viewport->IsRenderingAvailable();}
std::string SceneViewUI::GetRenderError() const{return _Viewport?_Viewport->GetRenderError():"Scene viewport is not initialized";}
bool SceneViewUI::RegisterSceneTool(const void* owner,ISceneTool* tool){return _Viewport&&_Viewport->Tools().Register(owner,tool);}
bool SceneViewUI::ActivateSceneTool(ISceneTool* tool){if(!_Viewport)return false;const bool activated=_Viewport->Tools().Activate(tool);if(activated)_Viewport->RequestRender();return activated;}
void SceneViewUI::DeactivateSceneTool(ISceneTool* tool){if(_Viewport)_Viewport->Tools().Deactivate(tool);}
ISceneTool* SceneViewUI::GetActiveSceneTool() const{return _Viewport?_Viewport->Tools().Active():nullptr;}
void SceneViewUI::UnregisterSceneTools(const void* owner){if(_Viewport)_Viewport->Tools().Unregister(owner);}
bool SceneViewUI::FocusSelection() {
    if(!_Viewport||!_Scenes||!_Selection||!_Runtime)return false;
    _Viewport->Editing().Cancel();
    _Viewport->Tools().CancelDrag();
    auto* runtime=_Runtime;auto* scene=_Scenes->GetActiveScene();const auto selection=_Selection->GetSceneObjectSelection();
    if(!runtime||!scene||selection.SceneID!=scene->GetInstanceID())return false;
    auto* object=runtime->QueryGameObject(selection.ObjectID);if(!object||object->GetScene()!=scene)return false;
    const float large=std::numeric_limits<float>::max();
    GameSproutRuntime::Vector3 minimum{large,large,large},maximum{-large,-large,-large};
    bool found=false,foundSizedBounds=false;
    std::vector<GameSproutRuntime::IGameObject*> pending{object};
    while(!pending.empty()) {
        auto* node=pending.back();pending.pop_back();
        // Renderer supplies bounds from the current skin pose; the editor does no skinning.
        GameSproutRuntime::Bounds posedBounds;
        if (_Viewport->Renderer() && node->GetComponent<GameSproutRuntime::IMeshComponent>() &&
            _Viewport->Renderer()->GetWorldBounds(scene,node->GetInstanceID(),posedBounds)) {
            const auto low=posedBounds.Center-posedBounds.Extents, high=posedBounds.Center+posedBounds.Extents;
            if (!foundSizedBounds) {minimum=low;maximum=high;}
            else {minimum={std::min(minimum.X,low.X),std::min(minimum.Y,low.Y),std::min(minimum.Z,low.Z)};
                maximum={std::max(maximum.X,high.X),std::max(maximum.Y,high.Y),std::max(maximum.Z,high.Z)};}
            found=true;foundSizedBounds=true;
            for(std::size_t index=0;index<node->GetChildCount();++index)pending.push_back(node->GetChild(index));
            continue;
        }
        const auto matrix=node->GetTransform()->GetWorldMatrix();
        // 没有几何体的节点只提供 pivot；存在非零 Mesh bounds 时忽略这些 pivot。
        GameSproutRuntime::Bounds bounds;
        if(auto* mesh=node->GetComponent<GameSproutRuntime::IMeshComponent>()) {
            const auto loaded=mesh->GetLocalBounds();
            if(std::isfinite(loaded.Center.X)&&std::isfinite(loaded.Center.Y)&&std::isfinite(loaded.Center.Z)&&
               std::isfinite(loaded.Extents.X)&&std::isfinite(loaded.Extents.Y)&&std::isfinite(loaded.Extents.Z)&&
               loaded.Extents.X>=0&&loaded.Extents.Y>=0&&loaded.Extents.Z>=0) bounds=loaded;
        }
        GameSproutRuntime::Vector3 nodeMinimum{large,large,large},nodeMaximum{-large,-large,-large};
        bool nodeFound=false;
        for(int corner=0;corner<8;++corner) {
            const auto local=bounds.Center+GameSproutRuntime::Vector3{corner&1?bounds.Extents.X:-bounds.Extents.X,corner&2?bounds.Extents.Y:-bounds.Extents.Y,corner&4?bounds.Extents.Z:-bounds.Extents.Z};
            const auto point=matrix*GameSproutRuntime::Vector4(local.X,local.Y,local.Z,1);
            if(!std::isfinite(point.X)||!std::isfinite(point.Y)||!std::isfinite(point.Z))continue;
            nodeMinimum={std::min(nodeMinimum.X,point.X),std::min(nodeMinimum.Y,point.Y),std::min(nodeMinimum.Z,point.Z)};
            nodeMaximum={std::max(nodeMaximum.X,point.X),std::max(nodeMaximum.Y,point.Y),std::max(nodeMaximum.Z,point.Z)};
            nodeFound=true;
        }
        if(nodeFound) {
            const bool sized=nodeMinimum.X<nodeMaximum.X||nodeMinimum.Y<nodeMaximum.Y||nodeMinimum.Z<nodeMaximum.Z;
            if(sized&&!foundSizedBounds) {minimum=nodeMinimum;maximum=nodeMaximum;found=true;}
            else if(sized||!foundSizedBounds) {
                minimum={std::min(minimum.X,nodeMinimum.X),std::min(minimum.Y,nodeMinimum.Y),std::min(minimum.Z,nodeMinimum.Z)};
                maximum={std::max(maximum.X,nodeMaximum.X),std::max(maximum.Y,nodeMaximum.Y),std::max(maximum.Z,nodeMaximum.Z)};
                found=true;
            }
            foundSizedBounds=foundSizedBounds||sized;
        }
        for(std::size_t index=0;index<node->GetChildCount();++index)pending.push_back(node->GetChild(index));
    }
    if(!found)return false;
    const float aspect = _Viewport->height() > 0 ? static_cast<float>(_Viewport->width()) / _Viewport->height() : 1.0f;
    return _Viewport->Camera().Focus(minimum*0.5f+maximum*0.5f,(maximum*0.5f-minimum*0.5f).Length(),aspect);
}

void SceneViewUI::OnPlayModeStateChanged(const PlayModeState& state) {
    if(_Viewport)_Viewport->RequestRender();
    if(_Viewport&&_ToolPlayStateKnown&&_ToolPlayState!=state.State)_Viewport->Tools().Deactivate();
    _ToolPlayStateKnown=true;_ToolPlayState=state.State;
    _CanEdit=state.State==EditorPlayState::Edit;
    if(!_Window||!_Ui)return;
    if(_Viewport)_Viewport->Editing().SetReadOnly(!_CanEdit);
    if(_Chrome)_Chrome->SetEditingEnabled(_CanEdit);
    QWidget* controls[]={_Ui->moveToolButton,_Ui->rotateToolButton,_Ui->scaleToolButton,_Ui->spaceCombo,_Ui->pivotCombo};
    for(QWidget* control:controls)control->setEnabled(_CanEdit);
}
