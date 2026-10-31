#include "ProjectAssetsUI.h"
#include "ISceneManager.h"
#include "IRuntimeAssets.h"
#include "ILibraryManager.h"
#include "ui_CreateAnimatorControllerDialog.h"
#include "ui_CreateAvatarMaskDialog.h"
#include "ui_ProjectAssetsUI.h"
#include <QDialog>
#include <QFileInfo>

bool ProjectAssetsUI::ResolveModelReference(GameSproutRuntime::ResourceReference& reference,std::string& error) const {
    reference={};
    const auto selected=_HasContextDirectory&&!_ContextAsset.GuidString.empty()?_ContextAsset:
        (GetSelection().empty()?ProjectAssetSelection{}:GetSelection().front());
    if (!_Library||selected.GuidString.empty()) {error="请先选择一个已经导入的模型资源。";return false;}
    AssetImportManifest manifest;
    if (!_Library->GetManifest(Guid(selected.GuidString),manifest)) {error="模型尚未完成导入。";return false;}
    for (const auto& asset:manifest.Assets) if (asset.Type==eAssetType::Model&&asset.SourceKey=="main") {
        reference={selected.GuidString,asset.LocalID};return true;
    }
    error="所选资源不包含 Model 主资源。";return false;
}
void ProjectAssetsUI::CreateAnimatorControllerMenu(){if(g_Self)g_Self->ShowCreateAnimatorControllerDialog();}
void ProjectAssetsUI::InstantiateModelMenu(){if(g_Self)g_Self->InstantiateSelectedModel();}
void ProjectAssetsUI::InstantiateSelectedModel() {
    if(!_Scenes||!_Window)return;
    GameSproutRuntime::ResourceReference reference;std::string error;
    if(!ResolveModelReference(reference,error)){_Ui->statusLabel->setText(QString::fromStdString(error));return;}
    _HasContextDirectory=false;
    auto* object=_Scenes->InstantiateModel(reference,0,error);
    if(!object){_Ui->statusLabel->setText(QString::fromStdString(error));return;}
    if(_Selection)_Selection->SetSceneObjectSelection({_Scenes->GetActiveScene()->GetInstanceID(),object->GetInstanceID()});
    _Ui->statusLabel->setText(QStringLiteral("模型和骨架已创建，可在 Inspector 或 Game 中预览动画。"));
}
void ProjectAssetsUI::ShowCreateAnimatorControllerDialog() {
    if(!_Window||!_RuntimeAssets)return;
    GameSproutRuntime::ResourceReference model;std::string error;
    if(!ResolveModelReference(model,error)){_Ui->statusLabel->setText(QString::fromStdString(error));return;}
    const QString directory=_HasContextDirectory?_ContextDirectory:_CurrentPath;_HasContextDirectory=false;
    QDialog dialog(_Window);Ui::CreateAnimatorControllerDialog ui;ui.setupUi(&dialog);
    ui.directoryLabel->setText(directory.isEmpty()?QStringLiteral("Assets"):QStringLiteral("Assets/")+directory);
    QObject::connect(ui.buttonBox,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QObject::connect(ui.buttonBox,&QDialogButtonBox::accepted,&dialog,[&]{
        std::string path,failure;
        if(!_RuntimeAssets->CreateAnimatorControllerAsset(directory.toStdString(),ui.controllerNameEdit->text().toStdString(),model,path,failure)) {
            ui.errorLabel->setText(QString::fromStdString(failure));ui.errorLabel->show();return;
        }
        _PendingCreatedAssetPath=QString::fromStdString(path);_Dirty=true;dialog.accept();
    });
    ui.controllerNameEdit->selectAll();dialog.exec();
}

void ProjectAssetsUI::CreateAvatarMaskMenu(){if(g_Self)g_Self->ShowCreateAvatarMaskDialog();}
void ProjectAssetsUI::ShowCreateAvatarMaskDialog(){
    if(!_Window||!_RuntimeAssets)return;GameSproutRuntime::ResourceReference model;std::string error;
    if(!ResolveModelReference(model,error)){_Ui->statusLabel->setText(QString::fromStdString(error));return;}
    const QString directory=_HasContextDirectory?_ContextDirectory:_CurrentPath;_HasContextDirectory=false;
    QDialog dialog(_Window);Ui::CreateAvatarMaskDialog ui;ui.setupUi(&dialog);
    ui.directoryLabel->setText(directory.isEmpty()?QStringLiteral("Assets"):QStringLiteral("Assets/")+directory);
    QObject::connect(ui.buttonBox,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QObject::connect(ui.buttonBox,&QDialogButtonBox::accepted,&dialog,[&]{
        std::string path,failure;
        if(!_RuntimeAssets->CreateAvatarMaskSource(directory.toStdString(),ui.maskNameEdit->text().toStdString(),model,path,failure)){
            ui.errorLabel->setText(QString::fromStdString(failure));ui.errorLabel->show();return;
        }
        _PendingCreatedAssetPath=QString::fromStdString(path);_Dirty=true;dialog.accept();
    });ui.maskNameEdit->selectAll();dialog.exec();
}
