#include "GameViewport.h"
#include "GameSproutRuntimeApi.h"
#include "GameSproutRuntimeExport.h"
#include "ui_GameViewUI.h"
#include <QApplication>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QTest>
#include <algorithm>
#include <iostream>
#include <stdexcept>
static void Check(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
class InputService final:public GameSproutRuntime::IGameSproutRuntime {
public:
    std::vector<GameSproutRuntime::RuntimeInputEvent> Events;
    bool Focused=false;
    GameSproutRuntime::IGameSproutRuntime* Real=nullptr;
    bool SubmitInputEvent(const GameSproutRuntime::RuntimeInputEvent& event)override{
        if(Real&&!Real->SubmitInputEvent(event))return false;
        if(event.Type==GameSproutRuntime::InputEventType::FocusGained)Focused=true;
        if(event.Type==GameSproutRuntime::InputEventType::FocusLost)Focused=false;
        Events.push_back(event);return true;
    }
    GameSproutRuntime::IRuntimeViewport* CreateViewport(const GameSproutRuntime::NativeWindow&,GameSproutRuntime::IRuntimeResourceProvider*,std::string&)override{return nullptr;}
    GameSproutRuntime::IRuntimeViewport* CreateOffscreenRenderer(uint32_t,uint32_t,GameSproutRuntime::IRuntimeResourceProvider*,std::string&)override{return nullptr;}
    GameSproutRuntime::IScene* CreateScene(const std::string&)override{return nullptr;}
    bool DestroyScene(GameSproutRuntime::IScene*)override{return false;}
    std::size_t GetSceneCount()const override{return 0;}
    GameSproutRuntime::IScene* GetScene(std::size_t)const override{return nullptr;}
    GameSproutRuntime::IGameObject* QueryGameObject(uint64_t)const override{return nullptr;}
    GameSproutRuntime::RuntimeProjectSettings FallbackSettings;
    const GameSproutRuntime::RuntimeProjectSettings& GetProjectSettings() const override {
        return Real?Real->GetProjectSettings():FallbackSettings;
    }
    bool CanApplyProjectSettings(const GameSproutRuntime::RuntimeProjectSettings& value,std::string& error) const override {
        return Real?Real->CanApplyProjectSettings(value,error):GameSproutRuntime::ValidateProjectSettings(value,error);
    }
    bool SetProjectSettings(const GameSproutRuntime::RuntimeProjectSettings& value,std::string& error) override {
        if(Real)return Real->SetProjectSettings(value,error);
        if(!CanApplyProjectSettings(value,error))return false;
        FallbackSettings=value;return true;
    }
    GameSproutRuntime::IGameObject* FindGameObjectWithTag(const std::string& name,const GameSproutRuntime::IScene* scene=nullptr) const override {
        return Real?Real->FindGameObjectWithTag(name,scene):nullptr;
    }
    std::vector<GameSproutRuntime::IGameObject*> FindGameObjectsWithTag(const std::string& name,const GameSproutRuntime::IScene* scene=nullptr) const override {
        return Real?Real->FindGameObjectsWithTag(name,scene):std::vector<GameSproutRuntime::IGameObject*>{};
    }
    bool RegisterListener(GameSproutRuntime::IRuntimeListener*)override{return true;}
    void UnregisterListener(GameSproutRuntime::IRuntimeListener*)override{}
    bool IsExecuting()const override{return false;}
    bool BeginConstruction(std::string& error)override{error.clear();return true;}
    bool EndConstruction(std::string& error)override{error.clear();return true;}
    bool EndFrame(std::string& error)override{error.clear();return true;}
    void Update(float)override{}
    void AdvanceFrame(float seconds,std::uint32_t fixedSteps,float fixedSeconds)override{
        if(Real)Real->AdvanceFrame(seconds,fixedSteps,fixedSeconds);
    }
    void Render()override{}
};
static void Pump(){QCoreApplication::processEvents();}
int main(int argc,char** argv){
    QApplication app(argc,argv);
    try{
        InputService runtime;
        if(argc>1){runtime.Real=Export(argv[1]);Check(runtime.Real,"Cannot load native Runtime");std::string error;Check(runtime.Real->BeginExecution(error),"Cannot begin native execution");}
        QWidget window;Ui::GameViewUIWindow ui;ui.setupUi(&window);window.resize(640,360);
        auto* game=ui.gameViewport;game->Initialize(&runtime,nullptr);
        window.show();window.activateWindow();Pump();
        QTest::keyClick(game,Qt::Key_W);Check(runtime.Events.empty(),"Edit input entered Runtime");
        game->setFocus();Pump();
        game->SetGameplayInputEnabled(true);Check(runtime.Events.empty(),"Play stole input focus");
        QTest::mouseClick(game,Qt::LeftButton,Qt::NoModifier,QPoint(25,25));Check(runtime.Focused,"Already-focused Edit viewport could not capture on Play click");
        game->ReleaseGameInput();runtime.Events.clear();
        ui.retryButton->setFocus();Pump();QTest::keyClick(ui.retryButton,Qt::Key_W);Check(runtime.Events.empty(),"Editor toolbar keys entered game");
        QTest::mousePress(game,Qt::LeftButton,Qt::NoModifier,QPoint(80,60));
        Check(runtime.Focused&&game->hasFocus(),"Game click did not acquire focus");
        Check(runtime.Events.size()>=3&&runtime.Events[0].Type==GameSproutRuntime::InputEventType::FocusGained,"Game input missing focus gate");
        Check(runtime.Events.back().Type==GameSproutRuntime::InputEventType::MouseButtonDown,"Mouse down not forwarded");
        Check(runtime.Events[runtime.Events.size()-2].X==80&&runtime.Events[runtime.Events.size()-2].Y==60,"Mouse position is not logical local pixels");
        QTest::mouseRelease(game,Qt::LeftButton,Qt::NoModifier,QPoint(80,60));
        QTest::keyPress(game,Qt::Key_W);Check(runtime.Events.back().Type==GameSproutRuntime::InputEventType::KeyDown&&runtime.Events.back().Key==GameSproutRuntime::InputKey::W,"W mapping failed");
        if(runtime.Real)Check(runtime.Real->GetInputState().GetKey(GameSproutRuntime::InputKey::W).Held,"Native script input did not receive focused W");
        auto count=runtime.Events.size();QKeyEvent repeat(QEvent::KeyPress,Qt::Key_W,Qt::NoModifier,"w",true);QCoreApplication::sendEvent(game,&repeat);Check(runtime.Events.size()==count,"Auto-repeat produced extra key transition");
        QTest::keyRelease(game,Qt::Key_W);Check(runtime.Events.back().Type==GameSproutRuntime::InputEventType::KeyUp,"Key release not forwarded");
        QTest::keyClick(game,Qt::Key_Tab);Check(runtime.Focused&&game->hasFocus()&&runtime.Events.back().Key==GameSproutRuntime::InputKey::Tab,"Tab escaped game capture");
        QWheelEvent wheel(QPointF(80,60),QPointF(game->mapToGlobal(QPoint(80,60))),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QCoreApplication::sendEvent(game,&wheel);
        Check(runtime.Events.back().Type==GameSproutRuntime::InputEventType::MouseWheel&&runtime.Events.back().WheelY==1,"Wheel detent conversion failed");
        QTest::keyClick(game,Qt::Key_Escape);Check(!runtime.Focused&&!game->hasFocus()&&runtime.Events.back().Type==GameSproutRuntime::InputEventType::FocusLost,"Escape failed to release input");
        if(runtime.Real)Check(!runtime.Real->GetInputState().Focused&&!runtime.Real->GetInputState().GetKey(GameSproutRuntime::InputKey::W).Held,"Native input stuck after Escape");
        count=runtime.Events.size();QTest::keyClick(game,Qt::Key_W);Check(runtime.Events.size()==count,"Keys reached game after Escape");
        QTest::mouseClick(game,Qt::LeftButton,Qt::NoModifier,QPoint(30,40));Check(runtime.Focused,"Click did not recapture");
        game->SetGameplayInputEnabled(false);Check(!runtime.Focused&&!game->hasFocus(),"Pause did not release focus/keys");
        count=runtime.Events.size();QTest::keyClick(game,Qt::Key_A);QTest::mouseClick(game,Qt::RightButton);Check(runtime.Events.size()==count,"Paused input reached Runtime");
        game->SetGameplayInputEnabled(true);Check(runtime.Events.size()==count,"Resume stole focus");
        QTest::mouseClick(game,Qt::LeftButton);Check(runtime.Focused,"Resume click not accepted");
        ui.retryButton->setFocus();Pump();Check(!runtime.Focused,"Focus loss left stuck game input");
        QTest::mouseClick(game,Qt::LeftButton);game->hide();Pump();Check(!runtime.Focused,"Hide left stuck game input");
        game->Shutdown();
        if(runtime.Real){std::string error;Check(runtime.Real->EndExecution(error),"Cannot end native execution");delete runtime.Real;runtime.Real=nullptr;}
        std::cout<<"PASS focused Play-only keyboard/mouse/wheel routing, repeat suppression, Tab, Escape, pause, focus loss and hide\n";
        return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';return 1;}
}
