#include "PlayModeUI.h"
#include "ui_PlayModeUI.h"
#include <QApplication>
#include <QSignalBlocker>

void PlayModeUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* editor) {
    g_Self=this; g_GameSproutEditor=editor;
    g_IIDEWindow=editor?editor->GetGameSproutView<IIDEWindow>("IDEWindow"):nullptr;
    _Play=editor?editor->GetGameSproutModule<IPlayMode>("PlayMode"):nullptr;
}
bool PlayModeUI::Initialize(GameSproutEditor::GameSproutViewApi*) {
    if (_Window) return true;
    if (!qobject_cast<QApplication*>(QCoreApplication::instance()) || !_Play || !g_IIDEWindow) return false;
    _Dispatch=new QObject(); _Window=new QWidget(); _Ui=new Ui::PlayModeUIWindow(); _Ui->setupUi(_Window);
    QObject::connect(_Window,&QObject::destroyed,_Dispatch,[this]{_Window=nullptr;_Attached=false;});
    QObject::connect(_Ui->playButton,&QToolButton::clicked,_Dispatch,[this] {
        std::string error;
        const bool accepted=_State.State==EditorPlayState::Edit?_Play->Play(error):_Play->Stop(error);
        ReportCommand(accepted,error);
    });
    QObject::connect(_Ui->pauseButton,&QToolButton::clicked,_Dispatch,[this] {
        std::string error;
        const bool accepted=_State.State==EditorPlayState::Paused?_Play->Resume(error):_Play->Pause(error);
        ReportCommand(accepted,error);
    });
    QObject::connect(_Ui->stepButton,&QToolButton::clicked,_Dispatch,[this] {
        std::string error;const bool accepted=_Play->Step(error);ReportCommand(accepted,error);
    });
    return true;
}
bool PlayModeUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    if (!_Window || !_Play || !g_IIDEWindow) return false;
    if (!g_IIDEWindow->AttachToolbarWidget(_Window)) return false;
    _Attached=true;
    _Subscribed=_Play->RegisterPlayModeListener(this);
    if (!_Subscribed) return false;
    OnPlayModeStateChanged(_Play->GetState());
    return true;
}
void PlayModeUI::Show(){if(_Window)_Window->show();}
void PlayModeUI::Hide(){if(_Window)_Window->hide();}
void PlayModeUI::ReportCommand(bool accepted,const std::string& error) {
    // Commands can be rejected; read once on explicit interaction to restore check state.
    if (_Play) OnPlayModeStateChanged(_Play->GetState());
    if (!accepted && _Ui && _Window) {
        const QString message=QString::fromStdString(error);
        _Ui->errorLabel->setText(_Ui->errorLabel->fontMetrics().elidedText(message.section('\n',0,0),Qt::ElideRight,_Ui->errorLabel->maximumWidth()));
        _Ui->errorLabel->setToolTip(message);
        _Ui->errorLabel->setVisible(!error.empty());
    }
}
void PlayModeUI::OnPlayModeStateChanged(const PlayModeState& state) {
    _State=state;
    if (!_Window || !_Ui) return;
    const bool editing=state.State==EditorPlayState::Edit;
    const bool playing=state.State==EditorPlayState::Playing;
    const bool paused=state.State==EditorPlayState::Paused;
    const bool entering=state.State==EditorPlayState::EnteringPlay;
    const QSignalBlocker playBlocker(_Ui->playButton),pauseBlocker(_Ui->pauseButton);
    _Ui->playButton->setChecked(!editing);
    _Ui->playButton->setEnabled(editing||playing||paused||entering||(state.State==EditorPlayState::ExitingPlay&&!state.Error.empty()));
    _Ui->pauseButton->setChecked(paused);
    _Ui->pauseButton->setEnabled(playing||paused);
    _Ui->stepButton->setEnabled(paused);
    const char* property=editing?"editText":entering?"enterText":state.State==EditorPlayState::ExitingPlay?"exitText":paused?"pauseText":"playText";
    _Ui->modeLabel->setText(_Window->property(property).toString());
    const QString message=QString::fromStdString(state.Error);
    _Ui->errorLabel->setText(_Ui->errorLabel->fontMetrics().elidedText(message.section('\n',0,0),Qt::ElideRight,_Ui->errorLabel->maximumWidth()));
    _Ui->errorLabel->setToolTip(message);
    _Ui->errorLabel->setVisible(!state.Error.empty());
}
void PlayModeUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if (_Subscribed && _Play) _Play->UnregisterPlayModeListener(this);
    _Subscribed=false;
    delete _Dispatch;_Dispatch=nullptr;
    if (_Attached && g_IIDEWindow && _Window) g_IIDEWindow->RemoveToolbarWidget(_Window);
    else delete _Window;
    _Window=nullptr;_Attached=false;
    delete _Ui;_Ui=nullptr;_Play=nullptr;
    if(g_Self==this)g_Self=nullptr;
}
