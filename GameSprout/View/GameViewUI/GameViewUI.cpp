#include "GameViewUI.h"
#include "GameViewport.h"
#include "ISceneManager.h"
#include "IRuntimeAssets.h"
#include "ui_GameViewUI.h"
#include <QApplication>
#include <QSignalBlocker>

GameViewUI::~GameViewUI(){Release(nullptr);}
void GameViewUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* editor) {
    g_Self=this;g_GameSproutEditor=editor;
    if(!editor)return;
    GetGameSproutView(editor,g_IIDEWindow,IDEWindow);
    GetGameSproutModule(editor,_Scenes,SceneManager);
    GetGameSproutModule(editor,_Resources,RuntimeAssets);
}
bool GameViewUI::Initialize(GameSproutEditor::GameSproutViewApi* editor) {
    if(_Window)return true;
    delete _Dispatch;_Dispatch=nullptr;delete _Ui;_Ui=nullptr;
    if(!qobject_cast<QApplication*>(QCoreApplication::instance()))return false;
    _Dispatch=new QObject();_Window=new QWidget();_Ui=new Ui::GameViewUIWindow();_Ui->setupUi(_Window);
    _Viewport=_Ui->gameViewport;
    _Viewport->Initialize(editor?editor->_GRHIApi:nullptr,_Scenes?_Scenes->GetRuntime():nullptr,
        _Resources?_Resources->GetResourceProvider():nullptr);
    _Viewport->StatusChanged=[this](const std::string& text){if(_Window)_Ui->statusLabel->setText(QString::fromStdString(text));};
    QObject::connect(_Ui->playButton,&QToolButton::toggled,_Dispatch,[this](bool playing){if(_Scenes)_Scenes->SetAnimationPreviewPlaying(playing);});
    QObject::connect(_Ui->stopButton,&QToolButton::clicked,_Dispatch,[this]{if(_Scenes)_Scenes->ResetAnimationPreview();});
    QObject::connect(_Ui->retryButton,&QToolButton::clicked,_Dispatch,[this]{if(_Viewport)_Viewport->RetryRendering();});
    QObject::connect(_Window,&QObject::destroyed,_Dispatch,[this]{_Window=nullptr;_Viewport=nullptr;_Docker=nullptr;_Attached=false;});
    return true;
}
bool GameViewUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    if(!_Window||!g_IIDEWindow)return false;
    _Docker=g_IIDEWindow->CreateDocker("Game");
    if(!_Docker||!_Docker->AttachWidget(_Window))return false;
    _Docker->SetTitle("Game");_Attached=true;return true;
}
void GameViewUI::LaunchFinished(GameSproutEditor::GameSproutViewApi*){}
void GameViewUI::Show(){if(_Window)_Window->show();if(_Docker)_Docker->Activate();}
void GameViewUI::Hide(){if(_Window)_Window->hide();}
void GameViewUI::Update(GameSproutEditor::GameSproutViewApi*) {
    if(!_Window)return;
    const QSignalBlocker blocker(_Ui->playButton);
    _Ui->playButton->setChecked(_Scenes&&_Scenes->IsAnimationPreviewPlaying());
    if(_Viewport)_Viewport->RenderGame(_Scenes?_Scenes->GetActiveScene():nullptr);
}
void GameViewUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if(_Viewport){_Viewport->StatusChanged={};_Viewport->Shutdown();}
    delete _Dispatch;_Dispatch=nullptr;
    if(!_Attached)delete _Window;
    _Window=nullptr;_Viewport=nullptr;_Docker=nullptr;_Attached=false;
    delete _Ui;_Ui=nullptr;_Scenes=nullptr;_Resources=nullptr;
    if(g_Self==this)g_Self=nullptr;
}
