#include "GameViewUI.h"
#include "GameViewport.h"
#include "ISceneManager.h"
#include "IRuntimeAssets.h"
#include "IAnimationPreview.h"
#include "ui_GameViewUI.h"
#include "ui_GameToolbarMenu.h"
#include <QApplication>
#include <QSignalBlocker>

GameViewUI::~GameViewUI(){Release(nullptr);}
void GameViewUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* editor) {
    g_Self=this;g_GameSproutEditor=editor;
    if(!editor)return;
    _PlayMode=editor->GetGameSproutModule<IPlayMode>("PlayMode");
    GetGameSproutView(editor,g_IIDEWindow,IDEWindow);
    GetGameSproutModule(editor,_Scenes,SceneManager);
    GetGameSproutModule(editor,_Host,RuntimeHost);
    _Runtime=editor?editor->_Runtime:nullptr;
    GetGameSproutModule(editor,_Preview,AnimationPreview);
    GetGameSproutModule(editor,_Resources,RuntimeAssets);
}
bool GameViewUI::Initialize(GameSproutEditor::GameSproutViewApi* editor) {
    if(_Window)return true;
    delete _Dispatch;_Dispatch=nullptr;delete _Ui;_Ui=nullptr;
    if(!qobject_cast<QApplication*>(QCoreApplication::instance()))return false;
    _Dispatch=new QObject();_Window=new QWidget();_Ui=new Ui::GameViewUIWindow();_Ui->setupUi(_Window);
    _Viewport=_Ui->gameViewport;
    auto* toolbarMenu=new QMenu(_Ui->gameToolbarOverflowButton);
    Ui::GameToolbarMenu toolbarMenuUi;
    toolbarMenuUi.setupUi(toolbarMenu);
    _Ui->gameToolbarOverflowButton->setMenu(toolbarMenu);
    _MaterialPreviewAction=toolbarMenuUi.materialPreviewAction;
    QObject::connect(_Viewport,&GameViewport::FrameSubmitted,_Ui->fpsLabel,&ViewportFrameRateLabel::FrameSubmitted);
    QObject::connect(_Viewport,&GameViewport::FrameStreamReset,_Ui->fpsLabel,&ViewportFrameRateLabel::Reset);
    _Viewport->Initialize(_Runtime,
        _Resources?_Resources->GetResourceProvider():nullptr);
    _Viewport->StatusChanged=[this](const std::string& text) {
        if(!_Window)return;
        const auto status=QString::fromStdString(text);
        _Ui->statusLabel->setText(status);
        _Ui->statusLabel->setToolTip(status);
    };
    QObject::connect(_Ui->playButton,&QToolButton::toggled,_Dispatch,[this](bool playing){
        if(_CanEdit&&_Preview)_Preview->SetAnimationPreviewPlaying(playing);
        // 命令可能被当前事务拒绝，立即回读一次确认按钮状态，不等待轮询。
        OnAnimationPreviewStateChanged(_Preview && _Preview->IsAnimationPreviewPlaying());
    });
    QObject::connect(_Ui->stopButton,&QToolButton::clicked,_Dispatch,[this]{
        if(_CanEdit&&_Preview)_Preview->ResetAnimationPreview();
        OnAnimationPreviewStateChanged(_Preview && _Preview->IsAnimationPreviewPlaying());
    });
    QObject::connect(toolbarMenuUi.retryRenderAction,&QAction::triggered,_Dispatch,[this]{if(_Viewport)_Viewport->RetryRendering();});
    QObject::connect(_MaterialPreviewAction,&QAction::toggled,_Dispatch,[this]{if(_Viewport)_Viewport->RequestRender();});
    QObject::connect(_Window,&QObject::destroyed,_Dispatch,[this]{_Window=nullptr;_Viewport=nullptr;_Docker=nullptr;_MaterialPreviewAction=nullptr;_Attached=false;});
    return true;
}
bool GameViewUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    if(!_Window||!g_IIDEWindow)return false;
    _Docker=g_IIDEWindow->CreateDocker("Game");
    if(!_Docker||!_Docker->AttachWidget(_Window))return false;
    _Docker->SetTitle("Game");_Attached=true;
    _DockerVisibilitySubscribed=_Docker->RegisterContentVisibilityCallback(this,
        [this](bool visible){_DockerContentVisible=visible;if(visible&&_Viewport)_Viewport->RequestRender();});
    _DockerContentVisible=_Docker->IsContentVisible();
    _LoadingSubscribed=g_IIDEWindow->RegisterLoadingStateCallback(this,[this](bool loading){_Loading=loading;if(!loading&&_Viewport)_Viewport->RequestRender();});
    _Loading=g_IIDEWindow->IsLoading();
    if(_Scenes)_SceneSubscribed=_Scenes->RegisterSceneListener(this);
    if(_Host)_FrameSubscribed=_Host->RegisterFrameListener(this);
    if(_Resources)_ResourcesSubscribed=_Resources->RegisterResourceListener(this);
    if(_Preview)_PreviewSubscribed=_Preview->RegisterPreviewListener(this);
    if(_PlayMode){_PlaySubscribed=_PlayMode->RegisterPlayModeListener(this);OnPlayModeStateChanged(_PlayMode->GetState());}
    OnActiveSceneChanged();OnAnimationPreviewStateChanged(_Preview && _Preview->IsAnimationPreviewPlaying());
    return _DockerVisibilitySubscribed && (!_Scenes || _SceneSubscribed) && _Host && _FrameSubscribed && (!_Preview || _PreviewSubscribed);
}
void GameViewUI::LaunchFinished(GameSproutEditor::GameSproutViewApi*){}
void GameViewUI::Show(){if(_Window)_Window->show();if(_Docker)_Docker->Activate();}
void GameViewUI::Hide(){if(_Window)_Window->hide();}
void GameViewUI::Update(GameSproutEditor::GameSproutViewApi*) {}
void GameViewUI::OnRuntimeFrame(const RuntimeFrameEvent& event) {
    if(_Loading||!_DockerContentVisible||!_Viewport||!_Window||!_Viewport->isVisible())return;
    const bool continuous=event.SimulationPlaying||(event.GameplayActive&&(!event.GameplayPaused||event.DeltaSeconds>0));
    const bool animateMaterials=!event.GameplayActive&&_MaterialPreviewAction->isChecked();
    _Viewport->RenderGame(_ActiveScene,continuous,animateMaterials);
}
void GameViewUI::OnActiveSceneChanged() {
    _ActiveScene=_Scenes?_Scenes->GetMainScene():nullptr;
    if(_Viewport)_Viewport->RequestRender();
}
void GameViewUI::OnAnimationPreviewStateChanged(bool scenePlaying) {
    if(_Viewport)_Viewport->RequestRender();
    if(!_Window || !_Ui)return;
    const QSignalBlocker blocker(_Ui->playButton);_Ui->playButton->setChecked(scenePlaying);
}
void GameViewUI::OnAnimationPreviewReset(){if(_Viewport)_Viewport->RequestRender();}
void GameViewUI::OnClipPreviewStateChanged(std::uint64_t,const GameSproutRuntime::AnimationClipPlaybackState&){if(_Viewport)_Viewport->RequestRender();}
void GameViewUI::OnRuntimeResourcesChanged(std::uint64_t){if(_Viewport)_Viewport->RequestRender();}
GameSproutRuntime::Vector2 GameViewUI::GetViewportSize() const {
    return _Viewport ? _Viewport->GetOutputSize() : GameSproutRuntime::Vector2{};
}
bool GameViewUI::RegisterViewportSizeCallback(const void* owner, const fViewportSizeCallback& callback) {
    if (!_Viewport || !_Dispatch || !owner || !callback || _ViewportSizeConnections.contains(owner)) return false;
    const auto connection = QObject::connect(_Viewport, &GameViewport::OutputSizeChanged, _Dispatch,
        [this, callback] { callback(GetViewportSize()); });
    if (!connection) return false;
    _ViewportSizeConnections.emplace(owner, connection);
    return true;
}
void GameViewUI::UnregisterViewportSizeCallback(const void* owner) {
    const auto found = _ViewportSizeConnections.find(owner);
    if (found == _ViewportSizeConnections.end()) return;
    QObject::disconnect(found->second);
    _ViewportSizeConnections.erase(found);
}
void GameViewUI::Release(GameSproutEditor::GameSproutViewApi*) {
    for (const auto& [owner, connection] : _ViewportSizeConnections) QObject::disconnect(connection);
    _ViewportSizeConnections.clear();
    if(_ResourcesSubscribed&&_Resources)_Resources->UnregisterResourceListener(this);
    _ResourcesSubscribed=false;
    if(_LoadingSubscribed && g_IIDEWindow)g_IIDEWindow->UnregisterLoadingStateCallback(this);
    _LoadingSubscribed=false;_Loading=false;
    if(_DockerVisibilitySubscribed && _Docker)_Docker->UnregisterContentVisibilityCallback(this);
    _DockerVisibilitySubscribed=false;_DockerContentVisible=false;
    if(_PlaySubscribed&&_PlayMode)_PlayMode->UnregisterPlayModeListener(this);
    _PlaySubscribed=false;_PlayMode=nullptr;_CanEdit=true;
    if(_FrameSubscribed && _Host)_Host->UnregisterFrameListener(this);
    if(_SceneSubscribed && _Scenes)_Scenes->UnregisterSceneListener(this);
    if(_PreviewSubscribed && _Preview)_Preview->UnregisterPreviewListener(this);
    _FrameSubscribed=false;_SceneSubscribed=false;_PreviewSubscribed=false;_ActiveScene=nullptr;_Host=nullptr;
    if(_Viewport){_Viewport->StatusChanged={};_Viewport->Shutdown();}
    delete _Dispatch;_Dispatch=nullptr;
    if(!_Attached)delete _Window;
    _Window=nullptr;_Viewport=nullptr;_Docker=nullptr;_MaterialPreviewAction=nullptr;_Attached=false;
    delete _Ui;_Ui=nullptr;_Scenes=nullptr;_Runtime=nullptr;_Preview=nullptr;_Resources=nullptr;
    if(g_Self==this)g_Self=nullptr;
}

void GameViewUI::OnPlayModeStateChanged(const PlayModeState& state) {
    if(_Viewport)_Viewport->RequestRender();
    _CanEdit=state.State==EditorPlayState::Edit;
    if(!_Window||!_Ui)return;
    _Ui->playButton->setEnabled(_CanEdit);_Ui->stopButton->setEnabled(_CanEdit);
    _MaterialPreviewAction->setEnabled(_CanEdit);
    if(_Viewport)_Viewport->SetGameplayInputEnabled(state.State==EditorPlayState::Playing);
}
