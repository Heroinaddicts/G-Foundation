#include "AnimationUI.h"
#include "IAnimationEditor.h"
#include "ILibraryManager.h"
#include "ui_CreateAnimatorControllerDialog.h"
#include "ui_CreateAvatarMaskDialog.h"
#include <QApplication>
#include <QDialog>

bool AnimationUI::ResolveModelReference(const ProjectAssetActionContext& context,GameSproutRuntime::ResourceReference& reference,std::string& error) const {
    reference={};
    if (!_Library || context.Selection.empty() || context.Selection.front().GuidString.empty()) {error="请先选择一个已经导入的模型资源。";return false;}
    AssetImportManifest manifest;
    const auto& selected=context.Selection.front();
    if (!_Library->GetManifest(Guid(selected.GuidString),manifest)) {error="模型尚未完成导入。";return false;}
    for (const auto& asset : manifest.Assets) if (asset.Type==eAssetType::Model && asset.SourceKey=="main") {
        reference={selected.GuidString,asset.LocalID};return true;
    }
    error="所选资源不包含 Model 主资源。";return false;
}
void AnimationUI::CreateAnimatorControllerMenu() { if (g_Self) g_Self->CreateAnimatorController(); }
void AnimationUI::CreateAvatarMaskMenu() { if (g_Self) g_Self->CreateAvatarMask(); }
void AnimationUI::CreateAnimatorController() {
    if (!_ProjectAssets || !_Authoring) return;
    const auto context=_ProjectAssets->GetActionContext();GameSproutRuntime::ResourceReference model;std::string error;
    if (!ResolveModelReference(context,model,error)) {qWarning().noquote()<<QString::fromStdString(error);return;}
    QDialog dialog(QApplication::activeWindow());Ui::CreateAnimatorControllerDialog ui;ui.setupUi(&dialog);
    ui.directoryLabel->setText(context.Directory.empty()?QStringLiteral("Assets"):QStringLiteral("Assets/")+QString::fromStdString(context.Directory));
    QObject::connect(ui.buttonBox,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QObject::connect(ui.buttonBox,&QDialogButtonBox::accepted,&dialog,[&]{
        std::string path,failure;
        if (!_Authoring->CreateAnimatorControllerAsset(context.Directory,ui.controllerNameEdit->text().toStdString(),model,path,failure)) {
            ui.errorLabel->setText(QString::fromStdString(failure));ui.errorLabel->show();return;
        }
        _ProjectAssets->NotifyAssetCreated(path);dialog.accept();
    });
    ui.controllerNameEdit->selectAll();dialog.exec();
}
void AnimationUI::CreateAvatarMask() {
    if (!_ProjectAssets || !_Authoring) return;
    const auto context=_ProjectAssets->GetActionContext();GameSproutRuntime::ResourceReference model;std::string error;
    if (!ResolveModelReference(context,model,error)) {qWarning().noquote()<<QString::fromStdString(error);return;}
    QDialog dialog(QApplication::activeWindow());Ui::CreateAvatarMaskDialog ui;ui.setupUi(&dialog);
    ui.directoryLabel->setText(context.Directory.empty()?QStringLiteral("Assets"):QStringLiteral("Assets/")+QString::fromStdString(context.Directory));
    QObject::connect(ui.buttonBox,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QObject::connect(ui.buttonBox,&QDialogButtonBox::accepted,&dialog,[&]{
        std::string path,failure;
        if (!_Authoring->CreateAvatarMaskSource(context.Directory,ui.maskNameEdit->text().toStdString(),model,path,failure)) {
            ui.errorLabel->setText(QString::fromStdString(failure));ui.errorLabel->show();return;
        }
        _ProjectAssets->NotifyAssetCreated(path);dialog.accept();
    });
    ui.maskNameEdit->selectAll();dialog.exec();
}
