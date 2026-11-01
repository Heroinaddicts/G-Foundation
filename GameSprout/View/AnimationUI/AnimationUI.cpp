#include "AnimationUI.h"
#include "AnimationAssetPanel.h"
#include "AnimatorComponentEditor.h"
#include "SkinnedMeshRendererComponentEditor.h"
#include "ModelAnimationSettingsEditor.h"
#include "AnimatorControllerDialog.h"
#include "AvatarMaskDialog.h"
#include "ModelRigDialog.h"
#include "IAnimationEditor.h"
#include "IAssetsManager.h"
#include "ILibraryManager.h"
#include "IInspectorUI.h"
#include "ui_AnimationUI.h"
#include <QApplication>
#include <QMenu>

AnimationUI::~AnimationUI() { Release(nullptr); }
void AnimationUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* editor) {
    g_Self=this;g_GameSproutEditor=editor;
    GetGameSproutView(editor,_Inspector,InspectorUI);
    GetGameSproutView(editor,_ProjectAssets,ProjectAssetsUI);
    GetGameSproutModule(editor,_Assets,AssetsManager);
    GetGameSproutModule(editor,_Library,LibraryManager);
    GetGameSproutModule(editor,_Selection,SelectionManager);
    GetGameSproutModule(editor,_Authoring,AnimationEditor);
}
bool AnimationUI::Initialize(GameSproutEditor::GameSproutViewApi*) {
    if (_Actions) return true;
    if (!qobject_cast<QApplication*>(QCoreApplication::instance())) return false;
    _Dispatch=new QObject();_Actions=new QMenu();_Ui=new Ui::AnimationActionsForm();_Ui->setupUi(_Actions);
    return true;
}
bool AnimationUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    if (!_Inspector || !_Authoring || !_Assets || !_Library || !_Actions) return false;
    if (!_Inspector->RegisterExtension(this)) return false;
    _Registered=true;
    if (_ProjectAssets) {
        if (!_ProjectAssets->RegisterAssetOpenHandler(this,[this](const ProjectAssetSelection& selection){return OpenAsset(selection);})) return false;
        const auto enabled=[this](const ProjectAssetActionContext& context) {
            GameSproutRuntime::ResourceReference model;std::string ignored;
            return ResolveModelReference(context,model,ignored);
        };
        const std::vector<std::string> base{_Ui->createMenu->title().toStdString(),_Ui->animationMenu->title().toStdString()};
        auto controller=base;controller.push_back(_Ui->controllerAction->text().toStdString());
        auto mask=base;mask.push_back(_Ui->maskAction->text().toStdString());
        if (!_ProjectAssets->RegisterContextAction(this,controller,&CreateAnimatorControllerMenu,enabled)
            || !_ProjectAssets->RegisterContextAction(this,mask,&CreateAvatarMaskMenu,enabled)) return false;
    }
    return true;
}
void AnimationUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if (_ProjectAssets) _ProjectAssets->UnregisterExtensions(this);
    if (_Registered && _Inspector) _Inspector->UnregisterExtension(this);
    _Registered=false;
    delete _RigDialog;_RigDialog=nullptr;
    delete _MaskDialog;_MaskDialog=nullptr;
    delete _ControllerDialog;_ControllerDialog=nullptr;
    delete _Dispatch;_Dispatch=nullptr;
    delete _Actions;_Actions=nullptr;delete _Ui;_Ui=nullptr;
    _Inspector=nullptr;_ProjectAssets=nullptr;_Authoring=nullptr;_Assets=nullptr;_Library=nullptr;_Selection=nullptr;
    if (g_Self==this) g_Self=nullptr;
    g_GameSproutEditor=nullptr;
}
void AnimationUI::Show() {
    if (_ControllerDialog) { _ControllerDialog->show();_ControllerDialog->raise();_ControllerDialog->activateWindow(); }
    else if (_RigDialog) { _RigDialog->show();_RigDialog->raise();_RigDialog->activateWindow(); }
    else if (_MaskDialog) { _MaskDialog->show();_MaskDialog->raise();_MaskDialog->activateWindow(); }
}
void AnimationUI::Hide() {
    if (_ControllerDialog) _ControllerDialog->hide();
    if (_RigDialog) _RigDialog->hide();
    if (_MaskDialog) _MaskDialog->hide();
}
IModelAnimationEditor* AnimationUI::CreateModelAnimationEditor(QWidget* parent) { return new ModelAnimationSettingsEditor(parent); }
bool AnimationUI::CanInspect(const InspectorServices& services,const InspectorTarget& target) const {
    if (target.Scene.ObjectID || target.Assets.size()!=1 || !services.Assets) return false;
    const auto& selected=target.Assets.front();
    auto* meta=services.Assets->QueryAsset(Guid(selected.GuidString));if (!meta) return false;
    if (!selected.IsSubAsset) return meta->_ImporterType==eAssetImporterType::AnimationClip
        || meta->_ImporterType==eAssetImporterType::AvatarMask || meta->_ImporterType==eAssetImporterType::AnimatorController;
    auto* asset=services.Library ? services.Library->QueryAsset(meta->_Guid,selected.LocalID) : nullptr;
    return asset && (asset->GetAssetType()==eAssetType::AnimationClip || asset->GetAssetType()==eAssetType::Avatar
        || asset->GetAssetType()==eAssetType::AvatarMask || asset->GetAssetType()==eAssetType::AnimatorController);
}
InspectorPanel* AnimationUI::CreatePanel(const InspectorServices& services,QWidget* parent) { return new AnimationAssetPanel(services,this,parent); }
ComponentEditor* AnimationUI::CreateComponentEditor(const std::type_info& type,QWidget* parent) {
    if (type==typeid(GameSproutRuntime::IAnimatorComponent)) return new AnimatorComponentEditor(parent);
    if (type==typeid(GameSproutRuntime::ISkinnedMeshRendererComponent)) return new SkinnedMeshRendererComponentEditor(parent);
    return nullptr;
}
bool AnimationUI::EditAnimatorController(const GameSproutRuntime::ResourceReference& reference) {
    if (!_Authoring || !_Dispatch || reference.Guid.empty()) return false;
    if (_ControllerDialog && _ControllerDialog->Reference()!=reference) {
        if (!_ControllerDialog->close()) return false;delete _ControllerDialog;_ControllerDialog=nullptr;
    }
    if (_ControllerDialog) { _ControllerDialog->show();_ControllerDialog->raise();_ControllerDialog->activateWindow();return true; }
    _ControllerDialog=new AnimatorControllerDialog(_Authoring,_Assets,_Library,_Selection,reference,QApplication::activeWindow());
    _ControllerDialog->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(_ControllerDialog,&QObject::destroyed,_Dispatch,[this]{_ControllerDialog=nullptr;});
    _ControllerDialog->open();return true;
}
bool AnimationUI::EditAvatarMask(const GameSproutRuntime::ResourceReference& reference) {
    if (!_Authoring || !_Dispatch || reference.Guid.empty()) return false;
    if (_MaskDialog && _MaskDialog->Reference()!=reference) {
        if (!_MaskDialog->close()) return false;delete _MaskDialog;_MaskDialog=nullptr;
    }
    if (_MaskDialog) { _MaskDialog->show();_MaskDialog->raise();_MaskDialog->activateWindow();return true; }
    _MaskDialog=new AvatarMaskDialog(_Authoring,g_GameSproutEditor && g_GameSproutEditor->_Runtime ? g_GameSproutEditor->_Runtime->GetResourceProvider() : nullptr,
        _Assets,_Library,_Selection,reference,QApplication::activeWindow());
    _MaskDialog->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(_MaskDialog,&QObject::destroyed,_Dispatch,[this]{_MaskDialog=nullptr;});
    _MaskDialog->open();return true;
}
bool AnimationUI::EditModelRig(const GameSproutRuntime::ResourceReference& reference) {
    if (!_Authoring || !_Dispatch || reference.Guid.empty()) return false;
    if (_RigDialog && _RigDialog->Reference()!=reference) {
        if (!_RigDialog->close()) return false;delete _RigDialog;_RigDialog=nullptr;
    }
    if (_RigDialog) { _RigDialog->show();_RigDialog->raise();_RigDialog->activateWindow();return true; }
    _RigDialog=new ModelRigDialog(_Authoring,reference,QApplication::activeWindow());
    _RigDialog->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(_RigDialog,&QObject::destroyed,_Dispatch,[this]{_RigDialog=nullptr;});
    _RigDialog->open();return true;
}
bool AnimationUI::OpenAsset(const ProjectAssetSelection& selected) {
    auto* meta=_Assets ? _Assets->QueryAsset(Guid(selected.GuidString)) : nullptr;
    if (!meta) return false;
    if (selected.IsSubAsset) {
        auto* asset=_Library ? _Library->QueryAsset(meta->_Guid,selected.LocalID) : nullptr;
        if (!asset) return false;
        const GameSproutRuntime::ResourceReference reference{selected.GuidString,selected.LocalID};
        if (asset->GetAssetType()==eAssetType::Avatar) { EditModelRig(reference);return true; }
        if (asset->GetAssetType()==eAssetType::AnimatorController) { EditAnimatorController(reference);return true; }
        if (asset->GetAssetType()==eAssetType::AvatarMask) { EditAvatarMask(reference);return true; }
        return false;
    }
    const bool controller=meta->_ImporterType==eAssetImporterType::AnimatorController;
    if (!controller && meta->_ImporterType!=eAssetImporterType::AvatarMask) return false;
    AssetImportManifest manifest;
    if (_Library && _Library->GetManifest(meta->_Guid,manifest)) {
        for (const auto& asset : manifest.Assets) if (asset.SourceKey=="main" && asset.Type==(controller ? eAssetType::AnimatorController : eAssetType::AvatarMask)) {
            const GameSproutRuntime::ResourceReference reference{selected.GuidString,asset.LocalID};
            if (controller) EditAnimatorController(reference);else EditAvatarMask(reference);
            return true;
        }
    }
    qWarning() << "Animation resource has not finished importing:" << QString::fromStdString(meta->GetPath());
    return true;
}
