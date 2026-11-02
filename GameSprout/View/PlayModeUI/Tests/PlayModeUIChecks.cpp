#include "PlayModeUI.h"
#include "IDEWindow.h"
#include <QApplication>
#include <QMainWindow>
#include <QLabel>
#include <QPointer>
#include <QToolBar>
#include <QToolButton>
#include <QTest>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace GameSproutEditor {
class GameSproutFinal {
public:
    static bool Start(IGameSproutView& view,GameSproutViewApi& editor) {
        view.BeforeInitialize(&editor);return view.Initialize(&editor)&&view.Launch(&editor);
    }
    static void Stop(IGameSproutView& view,GameSproutViewApi& editor){view.Release(&editor);}
};
}
static void Check(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
class PlayService final:public IPlayMode {
public:
    PlayModeState State;
    std::vector<IPlayModeListener*> Listeners;
    int Starts=0,Stops=0,Pauses=0,Resumes=0,Steps=0;
    bool RejectStep=false;
    PlayModeState GetState()const override{return State;}
    void Publish(EditorPlayState state){State={state,{}};const auto snapshot=Listeners;for(auto* listener:snapshot)listener->OnPlayModeStateChanged(State);}
    bool Play(std::string&)override{++Starts;Publish(EditorPlayState::EnteringPlay);return true;}
    bool Pause(std::string&)override{++Pauses;Publish(EditorPlayState::Paused);return true;}
    bool Resume(std::string&)override{++Resumes;Publish(EditorPlayState::Playing);return true;}
    bool Step(std::string& error)override{++Steps;if(RejectStep){error="单步已排队";return false;}return true;}
    bool Stop(std::string&)override{++Stops;Publish(EditorPlayState::Edit);return true;}
    bool RegisterPlayModeListener(IPlayModeListener* listener)override{Listeners.push_back(listener);return true;}
    void UnregisterPlayModeListener(IPlayModeListener* listener)override{std::erase(Listeners,listener);}
private:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
class Editor final:public GameSproutEditor::GameSproutViewApi {
public:
    Editor():GameSproutViewApi(nullptr){}
    IDEWindow IDE;
    PlayService Play;
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string& name)const override{return name=="IDEWindow"?const_cast<IDEWindow*>(&IDE):nullptr;}
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name)const override{return name=="PlayMode"?const_cast<PlayService*>(&Play):nullptr;}
};
int main(int argc,char** argv){
    QApplication app(argc,argv);
    try {
        Editor editor;PlayModeUI ui;
        Check(GameSproutEditor::GameSproutFinal::Start(editor.IDE,editor),"IDE start failed");
        Check(GameSproutEditor::GameSproutFinal::Start(ui,editor),"Play UI start failed");
        QMainWindow* window=nullptr;for(auto* candidate:QApplication::topLevelWidgets())if(candidate->objectName()=="IDEWindowWindow")window=qobject_cast<QMainWindow*>(candidate);
        Check(window,"Toolbar host missing");window->show();QCoreApplication::processEvents();
        auto* bar=window->findChild<QToolBar*>("editorToolBar");
        QPointer<QWidget> feature=window->findChild<QWidget*>("PlayModeUIWindow");
        Check(bar&&feature&&bar->isVisible(),"Global toolbar not attached");
        auto* play=feature->findChild<QToolButton*>("playButton");auto* pause=feature->findChild<QToolButton*>("pauseButton");auto* step=feature->findChild<QToolButton*>("stepButton");auto* error=feature->findChild<QLabel*>("errorLabel");
        Check(play&&pause&&step&&error,".ui controls missing");
        Check(!play->isChecked()&&!pause->isEnabled()&&!step->isEnabled(),"Invalid Edit controls");
        play->click();Check(editor.Play.Starts==1&&play->isChecked()&&play->isEnabled()&&!pause->isEnabled(),"EnteringPlay must allow cancel only");
        play->click();Check(editor.Play.Stops==1&&!play->isChecked(),"Pending Play cancel failed");
        play->click();editor.Play.Publish(EditorPlayState::Playing);
        Check(play->isChecked()&&pause->isEnabled()&&!step->isEnabled(),"Playing event not reflected");
        pause->click();Check(editor.Play.Pauses==1&&pause->isChecked()&&step->isEnabled(),"Pause command/event failed");
        step->click();Check(editor.Play.Steps==1&&pause->isChecked(),"Step must preserve pause");
        editor.Play.RejectStep=true;step->click();Check(error->text()==QString::fromUtf8("单步已排队")&&!error->isHidden(),"Rejected command must show error");
        pause->click();Check(editor.Play.Resumes==1&&!pause->isChecked()&&!step->isEnabled()&&error->isHidden(),"Resume did not restore controls");
        editor.Play.Publish(EditorPlayState::ExitingPlay);Check(!play->isEnabled()&&!pause->isEnabled()&&!step->isEnabled(),"Exiting must block all commands");
        editor.Play.State={EditorPlayState::ExitingPlay,"恢复失败，请重试停止"};
        for(auto* listener:editor.Play.Listeners)listener->OnPlayModeStateChanged(editor.Play.State);
        Check(play->isEnabled()&&!pause->isEnabled()&&!step->isEnabled(),"Failed stop must allow retry");
        play->click();Check(editor.Play.Stops==2&&!play->isChecked(),"Failed stop retry command failed");
        window->resize(800,500);QCoreApplication::processEvents();
        Check(feature->rect().contains(step->mapTo(feature,step->rect().center())),"Toolbar clipped at minimum width");
        editor.Play.Publish(EditorPlayState::Edit);
        Check(!editor.IDE.AttachToolbarWidget(feature),"Duplicate toolbar widget was accepted");
        GameSproutEditor::GameSproutFinal::Stop(ui,editor);
        Check(editor.Play.Listeners.empty(),"Release retained listener");
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        Check(feature.isNull(),"Toolbar extension not destroyed on release");
        Check(bar->actions().empty()&&bar->isHidden(),"Toolbar slot not removed");
        editor.Play.Publish(EditorPlayState::Playing);
        GameSproutEditor::GameSproutFinal::Stop(ui,editor);
        GameSproutEditor::GameSproutFinal::Stop(editor.IDE,editor);
        std::cout<<"PASS .ui global toolbar commands, asynchronous mode events, rejection, pending cancel and ordered release\n";
        return 0;
    } catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';return 1;}
}
