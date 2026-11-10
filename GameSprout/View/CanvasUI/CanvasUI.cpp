#include "CanvasUI.h"
#include "IUIAuthoring.h"
#include "IHierarchyUI.h"
#include "IInspectorUI.h"
#include "CanvasComponentEditor.h"
#include "RectTransformComponentEditor.h"
#include "ImageComponentEditor.h"
#include "TextComponentEditor.h"
#include "ButtonComponentEditor.h"
#include "ui_CanvasUI.h"
#include "ui_CanvasErrorDialog.h"
#include <QApplication>
#include <QDialog>
#include <QMenu>

namespace {CanvasUI* ActiveCanvasView=nullptr;}

CanvasUI::~CanvasUI(){Release(nullptr);}

void CanvasUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* editor) {
    ActiveCanvasView=this;
    _Authoring=editor?editor->GetGameSproutModule<IUIAuthoring>("UIAuthoring"):nullptr;
    _Hierarchy=editor?editor->GetGameSproutView<IHierarchyUI>("HierarchyUI"):nullptr;
    _Inspector=editor?editor->GetGameSproutView<IInspectorUI>("InspectorUI"):nullptr;
}

bool CanvasUI::Initialize(GameSproutEditor::GameSproutViewApi*) {
    if(_Actions)return true;
    if(!qobject_cast<QApplication*>(QCoreApplication::instance()))return false;
    _Actions=new QMenu();
    _Ui=new Ui::CanvasActionsForm();
    _Ui->setupUi(_Actions);
    return true;
}

template<UIControlTemplate Type>
void CanvasUI::CreateMenu(){if(ActiveCanvasView)ActiveCanvasView->Create(Type);}

bool CanvasUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    if(!_Authoring||!_Hierarchy||!_Inspector||!_Actions)return false;
    _InspectorRegistered=_Inspector->RegisterExtension(this);
    if(!_InspectorRegistered)return false;

    struct Entry {QAction* Action;IHierarchyUI::fContextActionCallback Callback;};
    const Entry entries[]={
        {_Ui->canvasAction,&CreateMenu<UIControlTemplate::Canvas>},
        {_Ui->emptyAction,&CreateMenu<UIControlTemplate::Empty>},
        {_Ui->panelAction,&CreateMenu<UIControlTemplate::Panel>},
        {_Ui->imageAction,&CreateMenu<UIControlTemplate::Image>},
        {_Ui->textAction,&CreateMenu<UIControlTemplate::Text>},
        {_Ui->buttonAction,&CreateMenu<UIControlTemplate::Button>}
    };
    const auto canCreate=[this](const HierarchyActionContext& context){
        std::string error;
        return _Authoring&&_Authoring->CanCreate(context.SceneID,context.ObjectID,error);
    };
    for(const auto& entry:entries) {
        if(_Hierarchy->RegisterContextAction(this,{_Actions->title().toStdString(),entry.Action->text().toStdString()},
            entry.Callback,canCreate))continue;
        _Hierarchy->UnregisterExtensions(this);
        _Inspector->UnregisterExtension(this);
        _InspectorRegistered=false;
        return false;
    }
    return true;
}

ComponentEditor* CanvasUI::CreateComponentEditor(const std::type_info& type,QWidget* parent) {
    if(type==typeid(GameSproutRuntime::ICanvasComponent))return new CanvasComponentEditor(parent);
    if(type==typeid(GameSproutRuntime::IRectTransformComponent))return new RectTransformComponentEditor(parent);
    if(type==typeid(GameSproutRuntime::IImageComponent))return new ImageComponentEditor(parent);
    if(type==typeid(GameSproutRuntime::ITextComponent))return new TextComponentEditor(parent,_Authoring);
    if(type==typeid(GameSproutRuntime::IButtonComponent))return new ButtonComponentEditor(parent,_Authoring);
    return nullptr;
}

void CanvasUI::Create(UIControlTemplate type) {
    if(!_Authoring||!_Hierarchy)return;
    const auto context=_Hierarchy->GetActionContext();
    std::uint64_t created=0;
    std::string error;
    if(!_Authoring->CreateControl(context.SceneID,context.ObjectID,type,created,error))Report(error);
}

void CanvasUI::Report(const std::string& error) {
    delete _ErrorDialog.data();
    auto* dialog=new QDialog(QApplication::activeWindow());
    _ErrorDialog=dialog;
    Ui::CanvasErrorDialogForm form;
    form.setupUi(dialog);
    form.messageLabel->setText(QString::fromStdString(error));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    QObject::connect(form.buttonBox,&QDialogButtonBox::accepted,dialog,&QDialog::accept);
    dialog->open();
}

void CanvasUI::Hide(){if(_ErrorDialog)_ErrorDialog->hide();}

void CanvasUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if(_Hierarchy)_Hierarchy->UnregisterExtensions(this);
    if(_InspectorRegistered&&_Inspector)_Inspector->UnregisterExtension(this);
    _InspectorRegistered=false;
    delete _ErrorDialog.data();
    _ErrorDialog=nullptr;
    delete _Actions;
    _Actions=nullptr;
    delete _Ui;
    _Ui=nullptr;
    _Authoring=nullptr;
    _Hierarchy=nullptr;
    _Inspector=nullptr;
    if(ActiveCanvasView==this)ActiveCanvasView=nullptr;
}
