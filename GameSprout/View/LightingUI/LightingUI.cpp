#include "LightingUI.h"
#include "LightComponentEditor.h"
#include "ILightingAuthoring.h"
#include "IHierarchyUI.h"
#include "IInspectorUI.h"
#include "IIDEWindow.h"
#include "EnvironmentLightingWindow.h"
#include "ui_LightingUI.h"
#include "ui_LightingErrorDialog.h"
#include <QApplication>
#include <QDialog>
#include <QMenu>

namespace {LightingUI* ActiveLightingView=nullptr;}
LightingUI::~LightingUI(){Release(nullptr);}
void LightingUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* editor) {
    ActiveLightingView=this;
    _Authoring=editor?editor->GetGameSproutModule<ILightingAuthoring>("LightingAuthoring"):nullptr;
    _Hierarchy=editor?editor->GetGameSproutView<IHierarchyUI>("HierarchyUI"):nullptr;
    _Inspector=editor?editor->GetGameSproutView<IInspectorUI>("InspectorUI"):nullptr;
    _IDE=editor?editor->GetGameSproutView<IIDEWindow>("IDEWindow"):nullptr;
}
bool LightingUI::Initialize(GameSproutEditor::GameSproutViewApi*) {
    if(_Actions)return true;if(!qobject_cast<QApplication*>(QCoreApplication::instance()))return false;
    _Actions=new QMenu();_Ui=new Ui::LightingActionsForm();_Ui->setupUi(_Actions);
    _EnvironmentWindow=new EnvironmentLightingWindow();return true;
}
bool LightingUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    if(!_Authoring||!_Hierarchy||!_Inspector||!_IDE||!_Actions||!_EnvironmentWindow)return false;
    if(!_EnvironmentWindow->Bind(_Authoring))return false;
    auto* bar=_IDE->GetMenuBar();auto* window=bar?bar->CreateChildMenu(_Ui->windowMenuAction->text()):nullptr;
    _EnvironmentMenu=window?window->CreateChildMenu(_Ui->environmentLightingAction->text()):nullptr;
    if(!_EnvironmentMenu)return false;
    _EnvironmentMenu->SetClickCallback(&ShowEnvironmentMenu);_EnvironmentMenu->SetEnabled(true);
    _Registered=_Inspector->RegisterExtension(this);if(!_Registered)return false;
    return _Hierarchy->RegisterContextAction(this,{_Ui->lightMenu->title().toStdString(),_Ui->directionalLightAction->text().toStdString()},&CreateMenu,
        [this](const HierarchyActionContext& context){std::string error;return _Authoring&&_Authoring->CanCreate(context.SceneID,context.ObjectID,error);});
}
ComponentEditor* LightingUI::CreateComponentEditor(const std::type_info& type,QWidget* parent) {
    return type==typeid(GameSproutRuntime::ILightComponent)?new LightComponentEditor(parent,_Authoring):nullptr;
}
void LightingUI::CreateMenu(){if(ActiveLightingView)ActiveLightingView->Create();}
void LightingUI::ShowEnvironmentMenu(){if(ActiveLightingView)ActiveLightingView->Show();}
void LightingUI::Create() {
    if(!_Authoring||!_Hierarchy)return;
    const auto context=_Hierarchy->GetActionContext();std::uint64_t created=0;std::string error;
    if(_Authoring->CreateDirectionalLight(context.SceneID,context.ObjectID,created,error))return;
    delete _ErrorDialog.data();auto* dialog=new QDialog(QApplication::activeWindow());_ErrorDialog=dialog;
    Ui::LightingErrorDialogForm form;form.setupUi(dialog);form.messageLabel->setText(QString::fromStdString(error));
    dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowModality(Qt::WindowModal);
    QObject::connect(form.buttonBox,&QDialogButtonBox::accepted,dialog,&QDialog::accept);dialog->open();
}
void LightingUI::Show(){if(_EnvironmentWindow){_EnvironmentWindow->show();_EnvironmentWindow->raise();_EnvironmentWindow->activateWindow();}}
void LightingUI::Hide(){if(_ErrorDialog)_ErrorDialog->hide();if(_EnvironmentWindow)_EnvironmentWindow->hide();}
void LightingUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if(_EnvironmentMenu){_EnvironmentMenu->SetClickCallback(nullptr);_EnvironmentMenu->SetEnabled(false);}_EnvironmentMenu=nullptr;
    if(_EnvironmentWindow)_EnvironmentWindow->Shutdown();
    if(_Hierarchy)_Hierarchy->UnregisterExtensions(this);
    if(_Registered&&_Inspector)_Inspector->UnregisterExtension(this);_Registered=false;
    delete _ErrorDialog.data();_ErrorDialog=nullptr;
    delete _EnvironmentWindow;_EnvironmentWindow=nullptr;
    delete _Actions;_Actions=nullptr;delete _Ui;_Ui=nullptr;
    _Authoring=nullptr;_Hierarchy=nullptr;_Inspector=nullptr;_IDE=nullptr;if(ActiveLightingView==this)ActiveLightingView=nullptr;
}
