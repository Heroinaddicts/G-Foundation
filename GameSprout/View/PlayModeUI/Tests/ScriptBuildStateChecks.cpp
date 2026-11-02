#include "ScriptComponentEditor.h"
#include "IScriptEditor.h"
#include <QApplication>
#include <QLabel>
#include <QToolButton>
#include <algorithm>
#include <iostream>
#include <stdexcept>
static void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
class Scripts final:public IScriptEditor {
public:
    ScriptBuildState State;
    std::vector<IScriptEditorListener*> Listeners;
    std::vector<EditorScriptType> GetScriptTypes()const override{return {};}
    ScriptBuildState GetBuildState()const override{return State;}
    bool RegisterScriptListener(IScriptEditorListener* listener)override{Listeners.push_back(listener);return true;}
    void UnregisterScriptListener(IScriptEditorListener* listener)override{std::erase(Listeners,listener);}
    bool CreateScript(const std::string&,const std::string&,std::string&,std::string&)override{return false;}
    bool RequestCompile(std::string&)override{return true;}
    void Publish(){for(auto* listener:Listeners)listener->OnScriptBuildStateChanged(State);}
private:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
int main(int argc,char** argv){
    QApplication app(argc,argv);
    try{
        Scripts scripts;
        {
            ScriptComponentEditor editor(nullptr);ComponentEditContext context;context.Scripts=&scripts;context.ObjectID=1;editor.Bind(context);
            auto* label=editor.findChild<QLabel*>("buildLabel");auto* compile=editor.findChild<QToolButton*>("compileButton");Check(label&&compile,"Missing existing build widgets");
            scripts.State.ReloadBlocked=true;scripts.State.PendingChanges=true;scripts.Publish();
            Check(label->text()==QString::fromUtf8("代码已修改，将重新运行并重置本次运行状态")&&!label->isHidden(),"Automatic restart state not displayed");
            Check(!compile->isEnabled(),"Play allowed immediate compile action");
            scripts.State.ReloadBlocked=false;scripts.State.Compiling=true;scripts.Publish();
            Check(label->text()==QString::fromUtf8("正在编译…")&&!compile->isEnabled(),"Active compilation lost display priority");
            scripts.State={};scripts.Publish();Check(label->isHidden()&&compile->isEnabled(),"Completed build did not clear deferred state");
        }
        Check(scripts.Listeners.empty(),"Script editor retained build listener");
        std::cout<<"PASS event-driven deferred/compiling/completed script build presentation\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';return 1;}
}
