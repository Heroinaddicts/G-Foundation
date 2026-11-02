#include "AssetOperationsUI.h"
#include "IProjectAssetsUI.h"
#include "ui_AssetOperationsUI.h"
#include "ui_DeleteAssetsDialog.h"
#include <QApplication>
#include <QDialog>
#include <QMenu>
#include <QAction>
#include <QKeySequence>

AssetOperationsUI::~AssetOperationsUI(){Release(nullptr);}
void AssetOperationsUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* editor) {
    g_Self=this;
    _Browser=editor?editor->GetGameSproutView<IProjectAssetsUI>("ProjectAssetsUI"):nullptr;
    _Operations=editor?editor->GetGameSproutModule<IAssetOperations>("AssetOperations"):nullptr;
}
bool AssetOperationsUI::Initialize(GameSproutEditor::GameSproutViewApi*) {
    if(_Actions)return true;
    if(!qobject_cast<QApplication*>(QCoreApplication::instance()))return false;
    _Dispatch=new QObject();_Actions=new QMenu();_Ui=new Ui::AssetOperationsActionsForm();_Ui->setupUi(_Actions);
    return _Browser&&_Operations;
}
bool AssetOperationsUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    if(!_Browser||!_Operations||!_Ui)return false;
    const auto enabled=[this](const ProjectAssetActionContext& context){return CanDelete(context);};
    return _Browser->RegisterContextAction(this,{_Ui->deleteAction->text().toStdString()},&DeleteMenu,enabled)&&
        _Browser->RegisterAssetShortcut(this,_Ui->deleteAction->shortcut().toString(QKeySequence::PortableText).toStdString(),&DeleteMenu,enabled)&&
        _Browser->RegisterAssetShortcut(this,_Ui->backspaceAction->shortcut().toString(QKeySequence::PortableText).toStdString(),&DeleteMenu,enabled);
}
void AssetOperationsUI::LaunchFinished(GameSproutEditor::GameSproutViewApi*) {}
void AssetOperationsUI::Update(GameSproutEditor::GameSproutViewApi*) {}
void AssetOperationsUI::Show(){if(_Dialog){_Dialog->show();_Dialog->raise();}}
void AssetOperationsUI::Hide(){if(_Dialog)_Dialog->hide();}
void AssetOperationsUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if(_Browser)_Browser->UnregisterExtensions(this);
    delete _Dialog.data();_Dialog=nullptr;delete _Dispatch;_Dispatch=nullptr;
    delete _Actions;_Actions=nullptr;delete _Ui;_Ui=nullptr;_Browser=nullptr;_Operations=nullptr;
    if(g_Self==this)g_Self=nullptr;
}
bool AssetOperationsUI::CanDelete(const ProjectAssetActionContext& context) const {
    if(!_Operations||context.Selection.empty())return false;
    for(const auto& item:context.Selection)if(item.IsSubAsset||item.GuidString.empty())return false;
    // Business rejections are explained by the .ui dialog, rather than leaving
    // an unexplained disabled command for an open scene or Prefab source.
    return true;
}
void AssetOperationsUI::DeleteMenu(){if(g_Self)g_Self->ConfirmDelete();}
void AssetOperationsUI::ConfirmDelete() {
    if(!_Browser||!_Operations||!_Dispatch)return;
    if(_Dialog){Show();return;}
    const auto context=_Browser->GetActionContext();
    std::vector<std::string> guids;
    for(const auto& item:context.Selection) {
        if(item.IsSubAsset)return;
        guids.push_back(item.GuidString);
    }
    AssetDeletePlan plan;std::string error;
    const bool prepared=_Operations->PrepareDelete(guids,plan,error);
    auto* dialog=new QDialog(QApplication::activeWindow());_Dialog=dialog;
    Ui::DeleteAssetsDialogForm form;form.setupUi(dialog);
    dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowModality(Qt::WindowModal);
    QStringList paths;for(const auto& target:plan.Sources)paths.push_back("Assets/"+QString::fromStdString(target.Path));
    form.pathsEdit->setPlainText(paths.join('\n'));form.errorLabel->setText(QString::fromStdString(error));
    form.errorLabel->setVisible(!error.empty());form.deleteButton->setEnabled(prepared);
    QObject::connect(form.cancelButton,&QPushButton::clicked,dialog,&QDialog::reject);
    QObject::connect(form.deleteButton,&QPushButton::clicked,dialog,[this,dialog,form,plan]{
        if(!_Operations)return;
        std::string error;
        if(!_Operations->Delete(plan,error)) {
            form.errorLabel->setText(QString::fromStdString(error));form.errorLabel->show();form.deleteButton->setEnabled(false);return;
        }
        dialog->accept();
    });
    dialog->open();
}
