#include "SceneEditorUI.h"
#include "IAssetsManager.h"
#include "ILibraryManager.h"
#include "ISelectionManager.h"
#include "ui_CreateSceneDialog.h"
#include "ui_SceneProjectActions.h"
#include <QDialog>
#include <QMenu>

bool SceneEditorUI::RegisterProjectActions() {
    if (!_ProjectAssets) return true; // 独立场景编辑宿主可以不加载资源浏览器。
    if (!_ProjectAssets->RegisterAssetOpenHandler(this,[this](const ProjectAssetSelection& selected) {return OpenProjectAsset(selected);})) return false;
    QMenu definition;
    Ui::SceneProjectActions ui;
    ui.setupUi(&definition);
    const bool scene=_ProjectAssets->RegisterContextAction(this,
        {ui.createMenu->title().toStdString(),ui.actionCreateScene->text().toStdString()},&CreateSceneMenu,
        [this](const ProjectAssetActionContext&) {return _CanEdit && _Scenes!=nullptr;});
    const bool model=_ProjectAssets->RegisterContextAction(this,
        {ui.actionInstantiateModel->text().toStdString()},&InstantiateModelMenu,
        [this](const ProjectAssetActionContext& context) {
            GameSproutRuntime::ResourceReference reference;std::string error;
            return _CanEdit && _Scenes && ResolveModelReference(context,reference,error);
        });
    if (!scene || !model) {_ProjectAssets->UnregisterExtensions(this);return false;}
    return true;
}

bool SceneEditorUI::OpenProjectAsset(const ProjectAssetSelection& selected) {
    if (!_Assets || selected.IsSubAsset) return false;
    const auto* meta=_Assets->QueryAsset(Guid(selected.GuidString));
    if (!meta || meta->_ImporterType!=eAssetImporterType::Scene) return false;
    OpenSceneAsset(meta->GetPath());
    return true;
}

bool SceneEditorUI::ResolveModelReference(const ProjectAssetActionContext& context,
    GameSproutRuntime::ResourceReference& reference,std::string& error) const {
    reference={};
    if (!_Library || context.Selection.size()!=1 || context.Selection.front().GuidString.empty()) {
        error="请先选择一个已经导入的模型资源。";return false;
    }
    AssetImportManifest manifest;
    const auto& selected=context.Selection.front();
    if (!_Library->GetManifest(Guid(selected.GuidString),manifest)) {error="模型尚未完成导入。";return false;}
    for (const auto& asset:manifest.Assets) if (asset.Type==eAssetType::Model && asset.SourceKey=="main") {
        reference={selected.GuidString,asset.LocalID};return true;
    }
    error="所选资源不包含 Model 主资源。";return false;
}

void SceneEditorUI::CreateSceneMenu() {if (_Self) _Self->ShowCreateSceneDialog();}
void SceneEditorUI::InstantiateModelMenu() {if (_Self) _Self->InstantiateSelectedModel();}

void SceneEditorUI::InstantiateSelectedModel() {
    if (!_CanEdit || !_Scenes || !_ProjectAssets) return;
    GameSproutRuntime::ResourceReference reference;std::string error;
    if (!ResolveModelReference(_ProjectAssets->GetActionContext(),reference,error)) {ShowError(error);return;}
    auto* object=_Scenes->InstantiateModel(reference,0,error);
    if (!object) {ShowError(error);return;}
    if (_Selection) _Selection->SetSceneObjectSelection({_Scenes->GetActiveScene()->GetInstanceID(),object->GetInstanceID()});
}

void SceneEditorUI::ShowCreateSceneDialog() {
    if (!_CanEdit || !_Scenes || !_ProjectAssets) return;
    if (_CreateSceneDialog) {_CreateSceneDialog->raise();_CreateSceneDialog->activateWindow();return;}
    const auto directory=_ProjectAssets->GetActionContext().Directory;
    QDialog* dialog=new QDialog();
    Ui::CreateSceneDialog ui;
    try {ui.setupUi(dialog);} catch (...) {delete dialog;throw;}
    _CreateSceneDialog=dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    ui.directoryLabel->setText(directory.empty()?QStringLiteral("Assets"):QStringLiteral("Assets/")+QString::fromStdString(directory));
    auto* name=ui.sceneNameEdit;auto* error=ui.errorLabel;
    QObject::connect(dialog,&QObject::destroyed,_Dispatch,[this,dialog] {if (_CreateSceneDialog==dialog) _CreateSceneDialog=nullptr;});
    QObject::connect(dialog,&QDialog::finished,_Dispatch,[this,dialog] {if (_CreateSceneDialog==dialog) _CreateSceneDialog=nullptr;});
    QObject::connect(ui.buttonBox,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    QObject::connect(ui.buttonBox,&QDialogButtonBox::accepted,dialog,[this,dialog,name,error,directory] {
        if (!_Scenes) {dialog->reject();return;}
        std::string path,message;
        if (!_Scenes->CreateSceneAsset(directory,name->text().toStdString(),path,message)) {
            error->setText(QString::fromStdString(message));error->show();name->setFocus();name->selectAll();return;
        }
        if (_ProjectAssets) _ProjectAssets->NotifyAssetCreated(path);
        dialog->accept();
    });
    name->selectAll();dialog->open();name->setFocus();
}
