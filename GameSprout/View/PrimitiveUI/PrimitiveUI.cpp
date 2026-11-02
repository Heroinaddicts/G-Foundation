#include "PrimitiveUI.h"
#include "IPrimitiveAuthoring.h"
#include "IHierarchyUI.h"
#include "ui_PrimitiveUI.h"
#include "ui_PrimitiveErrorDialog.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMenu>

namespace {PrimitiveUI* ActivePrimitiveView=nullptr;}

PrimitiveUI::~PrimitiveUI(){Release(nullptr);}
void PrimitiveUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* editor) {
    ActivePrimitiveView=this;
    _Authoring=editor?editor->GetGameSproutModule<IPrimitiveAuthoring>("PrimitiveAuthoring"):nullptr;
    _Hierarchy=editor?editor->GetGameSproutView<IHierarchyUI>("HierarchyUI"):nullptr;
}
bool PrimitiveUI::Initialize(GameSproutEditor::GameSproutViewApi*) {
    if(_Actions)return true;
    if(!qobject_cast<QApplication*>(QCoreApplication::instance()))return false;
    _Actions=new QMenu();_Ui=new Ui::PrimitiveActionsForm();_Ui->setupUi(_Actions);return true;
}
template<GameSproutRuntime::PrimitiveType Type>
void PrimitiveUI::CreateMenu(){if(ActivePrimitiveView)ActivePrimitiveView->Create(Type);}
bool PrimitiveUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    if(!_Authoring||!_Hierarchy||!_Actions)return false;
    struct Entry {QAction* Action;IHierarchyUI::fContextActionCallback Callback;};
    const Entry entries[]={
        {_Ui->cubeAction,&CreateMenu<GameSproutRuntime::PrimitiveType::Cube>},
        {_Ui->sphereAction,&CreateMenu<GameSproutRuntime::PrimitiveType::Sphere>},
        {_Ui->capsuleAction,&CreateMenu<GameSproutRuntime::PrimitiveType::Capsule>},
        {_Ui->cylinderAction,&CreateMenu<GameSproutRuntime::PrimitiveType::Cylinder>},
        {_Ui->planeAction,&CreateMenu<GameSproutRuntime::PrimitiveType::Plane>},
        {_Ui->quadAction,&CreateMenu<GameSproutRuntime::PrimitiveType::Quad>}};
    for(const auto& entry:entries)if(!_Hierarchy->RegisterContextAction(this,
        {_Ui->objectsMenu->title().toStdString(),entry.Action->text().toStdString()},entry.Callback,
        [this](const HierarchyActionContext& context){std::string error;return _Authoring&&_Authoring->CanCreate(context.SceneID,context.ObjectID,error);})) {
        _Hierarchy->UnregisterExtensions(this);return false;
    }
    return true;
}
void PrimitiveUI::Create(GameSproutRuntime::PrimitiveType type) {
    if(!_Authoring||!_Hierarchy)return;
    const auto context=_Hierarchy->GetActionContext();std::uint64_t created=0;std::string error;
    if(!_Authoring->CreatePrimitive(context.SceneID,context.ObjectID,type,created,error))Report(error);
}
void PrimitiveUI::Report(const std::string& error) {
    delete _ErrorDialog.data();
    auto* dialog=new QDialog(QApplication::activeWindow());_ErrorDialog=dialog;
    Ui::PrimitiveErrorDialogForm form;form.setupUi(dialog);form.messageLabel->setText(QString::fromStdString(error));
    dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowModality(Qt::WindowModal);
    QObject::connect(form.buttonBox,&QDialogButtonBox::accepted,dialog,&QDialog::accept);dialog->open();
}
void PrimitiveUI::Hide(){if(_ErrorDialog)_ErrorDialog->hide();}
void PrimitiveUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if(_Hierarchy)_Hierarchy->UnregisterExtensions(this);
    delete _ErrorDialog.data();_ErrorDialog=nullptr;
    delete _Actions;_Actions=nullptr;delete _Ui;_Ui=nullptr;
    _Authoring=nullptr;_Hierarchy=nullptr;if(ActivePrimitiveView==this)ActivePrimitiveView=nullptr;
}
