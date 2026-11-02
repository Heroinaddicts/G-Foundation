#include "PhysicsUI.h"
#include "ColliderComponentEditor.h"
#include "ColliderSceneTool.h"
#include "RigidbodyComponentEditor.h"
#include "PhysicsMaterialPanel.h"
#include "IInspectorUI.h"
#include "IProjectAssetsUI.h"
#include "IPhysicsAuthoring.h"
#include "ISceneViewUI.h"
#include "IAssetsManager.h"
#include "ILibraryManager.h"
#include "ui_PhysicsUI.h"
#include "ui_CreatePhysicsMaterialDialog.h"
#include <QApplication>
#include <QDialog>
#include <QMenu>
#include <algorithm>

PhysicsUI::~PhysicsUI(){Release(nullptr);}
void PhysicsUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* editor) {
    g_Self=this;g_GameSproutEditor=editor;
    _Inspector=editor?editor->GetGameSproutView<IInspectorUI>("InspectorUI"):nullptr;
    _ProjectAssets=editor?editor->GetGameSproutView<IProjectAssetsUI>("ProjectAssetsUI"):nullptr;
    _Authoring=editor?editor->GetGameSproutModule<IPhysicsAuthoring>("PhysicsAuthoring"):nullptr;
    _Assets=editor?editor->GetGameSproutModule<IAssetsManager>("AssetsManager"):nullptr;
    _SceneView=editor?editor->GetGameSproutView<ISceneViewUI>("SceneViewUI"):nullptr;
    _Runtime=editor?editor->_Runtime:nullptr;
}
bool PhysicsUI::Initialize(GameSproutEditor::GameSproutViewApi*) {
    if(_Actions)return true;if(!qobject_cast<QApplication*>(QCoreApplication::instance()))return false;
    _Dispatch=new QObject();_Actions=new QMenu();_Ui=new Ui::PhysicsActionsForm();_Ui->setupUi(_Actions);return true;
}
bool PhysicsUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    if(!_Inspector||!_Authoring||!_Assets||!_Actions)return false;
    if(_SceneView&&_Runtime) {
        _ColliderTool=new ColliderSceneTool(_Runtime,_Authoring,_SceneView);
        if(!_SceneView->RegisterSceneTool(this,_ColliderTool))return false;
    }
    _Registered=_Inspector->RegisterExtension(this);if(!_Registered)return false;
    if(_ProjectAssets) {
        if(!_ProjectAssets->RegisterContextAction(this,{_Ui->createMenu->title().toStdString(),_Ui->createPhysicsMaterialAction->text().toStdString()},&CreateMaterialMenu))return false;
        if(!_ProjectAssets->RegisterAssetOpenHandler(this,[this](const ProjectAssetSelection& selection){
            auto* meta=_Assets?_Assets->QueryAsset(Guid(selection.GuidString)):nullptr;
            if(!meta||meta->_ImporterType!=eAssetImporterType::PhysicsMaterial)return false;
            if(_Inspector)_Inspector->Show();return true;
        }))return false;
    }
    return true;
}
void PhysicsUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if(_SceneView)_SceneView->UnregisterSceneTools(this);
    for(const auto& editor:_ColliderEditors)if(editor)editor->DetachTools();
    _ColliderEditors.clear();
    if(_ProjectAssets)_ProjectAssets->UnregisterExtensions(this);
    if(_Registered&&_Inspector)_Inspector->UnregisterExtension(this);
    _Registered=false;delete _CreateDialog.data();_CreateDialog=nullptr;
    delete _ColliderTool;_ColliderTool=nullptr;
    delete _Dispatch;_Dispatch=nullptr;delete _Actions;_Actions=nullptr;delete _Ui;_Ui=nullptr;
    _Inspector=nullptr;_ProjectAssets=nullptr;_Authoring=nullptr;_Assets=nullptr;
    _SceneView=nullptr;_Runtime=nullptr;
    if(g_Self==this)g_Self=nullptr;g_GameSproutEditor=nullptr;
}
void PhysicsUI::Show(){if(_Inspector)_Inspector->Show();}
void PhysicsUI::Hide(){if(_CreateDialog)_CreateDialog->hide();}
bool PhysicsUI::CanInspect(const InspectorServices& services,const InspectorTarget& target) const {
    if(target.Scene.ObjectID||target.Assets.size()!=1||!services.Assets)return false;
    const auto& selected=target.Assets.front();auto* meta=services.Assets->QueryAsset(Guid(selected.GuidString));
    if(!meta||meta->_ImporterType!=eAssetImporterType::PhysicsMaterial)return false;
    if(!selected.IsSubAsset)return true;
    auto* asset=services.Library?services.Library->QueryAsset(meta->_Guid,selected.LocalID):nullptr;
    return asset&&asset->GetAssetType()==eAssetType::PhysicsMaterial;
}
InspectorPanel* PhysicsUI::CreatePanel(const InspectorServices& services,QWidget* parent){return new PhysicsMaterialPanel(services,_Authoring,parent);}
ComponentEditor* PhysicsUI::CreateComponentEditor(const std::type_info& type,QWidget* parent) {
    if(type==typeid(GameSproutRuntime::IBoxColliderComponent)||type==typeid(GameSproutRuntime::ISphereColliderComponent)||type==typeid(GameSproutRuntime::ICapsuleColliderComponent)||type==typeid(GameSproutRuntime::IMeshColliderComponent)) {
        auto* editor=new ColliderComponentEditor(parent,_Authoring,_ColliderTool);
        std::erase_if(_ColliderEditors,[](const auto& current){return current.isNull();});
        _ColliderEditors.push_back(editor);return editor;
    }
    if(type==typeid(GameSproutRuntime::IRigidbodyComponent))return new RigidbodyComponentEditor(parent);
    return nullptr;
}
void PhysicsUI::CreateMaterialMenu(){if(g_Self)g_Self->CreateMaterial();}
void PhysicsUI::CreateMaterial() {
    if(!_ProjectAssets||!_Authoring||!_Dispatch)return;
    if(_CreateDialog){_CreateDialog->show();_CreateDialog->raise();return;}
    const auto context=_ProjectAssets->GetActionContext();
    auto* dialog=new QDialog(QApplication::activeWindow());_CreateDialog=dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowModality(Qt::WindowModal);
    Ui::CreatePhysicsMaterialDialogForm form;form.setupUi(dialog);
    form.directoryLabel->setText(context.Directory.empty()?QStringLiteral("Assets"):QStringLiteral("Assets/")+QString::fromStdString(context.Directory));
    QObject::connect(dialog,&QObject::destroyed,_Dispatch,[this,dialog]{if(_CreateDialog==dialog)_CreateDialog=nullptr;});
    QObject::connect(form.buttonBox,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    QObject::connect(form.buttonBox,&QDialogButtonBox::accepted,dialog,[this,form,dialog,context]{
        if(!_Authoring||!_ProjectAssets)return;
        std::string path,error;
        if(!_Authoring->CreatePhysicsMaterialAsset(context.Directory,form.nameEdit->text().trimmed().toStdString(),path,error)){form.errorLabel->setText(QString::fromStdString(error));return;}
        _ProjectAssets->NotifyAssetCreated(path);dialog->accept();
    });
    form.nameEdit->selectAll();dialog->open();
}
