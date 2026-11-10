#include "ScriptUI.h"
#include "ScriptComponentEditor.h"
#include "IInspectorUI.h"
#include "IAssetsManager.h"
#include "ICodeEditor.h"
#include "IConsole.h"
#include "ui_ScriptUI.h"
#include "ui_CreateScriptDialog.h"
#include <QApplication>
#include <QMenu>
#include <QDialog>
#include <QFileInfo>
#include <exception>
ScriptUI::~ScriptUI(){Release(nullptr);}
void ScriptUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* editor){
    g_Self=this;g_GameSproutEditor=editor;
    GetGameSproutView(editor,_Inspector,InspectorUI);
    GetGameSproutView(editor,_ProjectAssets,ProjectAssetsUI);
    GetGameSproutModule(editor,_Scripts,ScriptEditor);
    GetGameSproutModule(editor,_Assets,AssetsManager);
    GetGameSproutModule(editor,_CodeEditor,CodeEditor);
    GetGameSproutModule(editor,_Console,Console);
}
bool ScriptUI::Initialize(GameSproutEditor::GameSproutViewApi*){
    if(!qobject_cast<QApplication*>(QCoreApplication::instance()))return false;
    _Actions=new QMenu();_Ui=new Ui::ScriptActionsForm;_Ui->setupUi(_Actions);return true;
}
bool ScriptUI::Launch(GameSproutEditor::GameSproutViewApi*){
    if(!_Scripts||!_Inspector||!_ProjectAssets||!_Assets||!_CodeEditor)return false;
    _Registered=_Inspector->RegisterExtension(this);
    if(!_Registered)return false;
    const std::string createMenu=_Ui->createMenu->title().toStdString();
    const std::string scriptMenu=_Ui->scriptMenu->title().toStdString();
    if(!_ProjectAssets->RegisterContextAction(this,{createMenu,scriptMenu,_Ui->createScriptAction->text().toStdString()},&CreateScriptMenu))return false;
    if(!_ProjectAssets->RegisterContextAction(this,{createMenu,scriptMenu,_Ui->createModuleAction->text().toStdString()},&CreateModuleMenu))return false;
    return _ProjectAssets->RegisterAssetOpenHandler(this,[this](const ProjectAssetSelection& selection){return OpenSourceAsset(selection);});
}
void ScriptUI::Release(GameSproutEditor::GameSproutViewApi*){
    if(_ProjectAssets)_ProjectAssets->UnregisterExtensions(this);
    if(_Registered&&_Inspector)_Inspector->UnregisterExtension(this);
    _Registered=false;delete _Dialog;_Dialog=nullptr;delete _DialogUi;_DialogUi=nullptr;
    delete _Actions;_Actions=nullptr;delete _Ui;_Ui=nullptr;
    _Scripts=nullptr;_Inspector=nullptr;_ProjectAssets=nullptr;_Assets=nullptr;_CodeEditor=nullptr;_Console=nullptr;
    if(g_Self==this)g_Self=nullptr;g_GameSproutEditor=nullptr;
}
void ScriptUI::Show(){if(_Dialog){_Dialog->show();_Dialog->raise();_Dialog->activateWindow();}}
void ScriptUI::Hide(){if(_Dialog)_Dialog->hide();}
bool ScriptUI::OpenSourceAsset(const ProjectAssetSelection& selected){
    if(!_Assets||selected.IsSubAsset||selected.LocalID)return false;
    const Guid guid(selected.GuidString);if(!guid.IsValid())return false;
    const auto* meta=_Assets->QueryAsset(guid);
    if(!meta||meta->_ImporterType!=eAssetImporterType::Script)return false;
    // Copy identity data before crossing the module boundary. The service owns
    // platform selection, canonical path validation and asynchronous generation.
    const std::string path=meta->GetPath();
    const QString suffix=QFileInfo(QString::fromStdString(path)).suffix().toLower();
    if(suffix!="hpp"&&suffix!="h"&&suffix!="hxx"&&suffix!="inl"&&suffix!="cpp"&&suffix!="cc"&&suffix!="cxx")return false;
    std::string error;
    try {
        if(!_CodeEditor)error="代码编辑服务尚未就绪。";
        else if(_CodeEditor->OpenSource(path,error))return true;
    } catch(const std::exception& exception){error=exception.what();}
      catch(...){error="打开游戏代码工程时发生未知错误。";}
    if(error.empty())error="无法打开游戏代码工程。";
    if(_Console)_Console->ConsoleLog(__FILE__,__LINE__,eConsoleLog::Error,error);
    else qWarning().noquote()<<QString::fromStdString(error);
    // This source belongs to the code-project handler even when launching fails;
    // falling through would open only a loose source file in the system default app.
    return true;
}
ComponentEditor* ScriptUI::CreateComponentEditor(const std::type_info& type,QWidget* parent){
    return type==typeid(GameSproutRuntime::IScriptComponent)?new ScriptComponentEditor(parent,_CodeEditor):nullptr;
}
void ScriptUI::CreateScriptMenu(){if(g_Self)g_Self->OpenCreateDialog(false);}
void ScriptUI::CreateModuleMenu(){if(g_Self)g_Self->OpenCreateDialog(true);}
void ScriptUI::OpenCreateDialog(bool isModule){
    if(!_Scripts||!_ProjectAssets)return;
    delete _Dialog;delete _DialogUi;
    _Dialog=new QDialog(QApplication::activeWindow());_DialogUi=new Ui::CreateScriptDialogForm;_DialogUi->setupUi(_Dialog);
    const auto* action=isModule?_Ui->createModuleAction:_Ui->createScriptAction;
    _Dialog->setWindowTitle(action->property("dialogTitle").toString());
    _DialogUi->nameEdit->setText(action->property("defaultName").toString());
    const std::string directory=_ProjectAssets->GetActionContext().Directory;
    _DialogUi->directoryLabel->setText(QString::fromStdString("Assets"+(directory.empty()?std::string():"/"+directory)));
    QObject::connect(_DialogUi->cancelButton,&QPushButton::clicked,_Dialog,&QDialog::reject);
    QObject::connect(_DialogUi->createButton,&QPushButton::clicked,_Dialog,[this,directory,isModule]{
        std::string path,error;
        const std::string name=_DialogUi->nameEdit->text().trimmed().toStdString();
        const bool created=isModule?_Scripts->CreateModule(directory,name,path,error)
                                   :_Scripts->CreateScript(directory,name,path,error);
        if(!created) {
            _DialogUi->errorLabel->setText(QString::fromStdString(error));return;
        }
        _ProjectAssets->NotifyAssetCreated(path);_Dialog->accept();
    });
    _DialogUi->nameEdit->selectAll();_Dialog->open();_DialogUi->nameEdit->setFocus();
}
