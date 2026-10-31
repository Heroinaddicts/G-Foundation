#include "SceneEditorUI.h"
#include "ui_SaveSceneDialog.h"
#include "ui_UnsavedSceneDialog.h"
#include "ui_SceneErrorDialog.h"
#include <QDialog>
#include <QApplication>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>

void SceneEditorUI::ShowSaveDialog(const std::function<void()>& afterSave) {
    if (_SaveDialog) { _SaveDialog->raise(); _SaveDialog->activateWindow(); return; }
    const std::uint64_t sceneID = SceneID();
    if (!sceneID) return;
    QDialog* dialog = new QDialog();
    Ui::SaveSceneDialog ui;
    ui.setupUi(dialog);
    _SaveDialog = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::ApplicationModal);
    ui.pathEdit->setText(_Scenes->GetActiveScenePath().empty() ? QStringLiteral("Untitled.scene") :
        QString::fromStdString(_Scenes->GetActiveScenePath()));
    auto* path = ui.pathEdit;
    auto* error = ui.errorLabel;
    QObject::connect(dialog,&QObject::destroyed,_Dispatch,[this,dialog] { if (_SaveDialog==dialog) _SaveDialog=nullptr; });
    QObject::connect(dialog,&QDialog::finished,_Dispatch,[this,dialog] { if (_SaveDialog==dialog) _SaveDialog=nullptr; });
    QObject::connect(ui.buttonBox,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    QObject::connect(ui.buttonBox,&QDialogButtonBox::accepted,dialog,[this,dialog,sceneID,path,error,afterSave] {
        if (SceneID()!=sceneID) { dialog->reject(); return; }
        std::string message;
        if (!_Scenes->SaveSceneAs(path->text().toStdString(),message)) {
            error->setText(QString::fromStdString(message)); error->show(); return;
        }
        dialog->accept();
        if (afterSave) afterSave();
    });
    dialog->open();
    path->selectAll(); path->setFocus();
}
void SceneEditorUI::OpenSceneAsset(const std::string& path) {
    if (!_Scenes) return;
    if (QWidget* focused = QApplication::focusWidget()) focused->clearFocus();
    if (!_Scenes->HasUnsavedChanges()) { OpenNow(path); return; }
    if (_UnsavedDialog || _SaveDialog) return;
    const std::uint64_t sceneID = SceneID();
    QDialog* dialog = new QDialog();
    Ui::UnsavedSceneDialog ui;
    ui.setupUi(dialog);
    _UnsavedDialog = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::ApplicationModal);
    QObject::connect(dialog,&QObject::destroyed,_Dispatch,[this,dialog] { if (_UnsavedDialog==dialog) _UnsavedDialog=nullptr; });
    QObject::connect(dialog,&QDialog::finished,_Dispatch,[this,dialog] { if (_UnsavedDialog==dialog) _UnsavedDialog=nullptr; });
    QObject::connect(ui.cancelButton,&QPushButton::clicked,dialog,&QDialog::reject);
    QObject::connect(ui.discardButton,&QPushButton::clicked,dialog,[this,dialog,sceneID,path] {
        if (SceneID()!=sceneID) { dialog->reject(); return; }
        dialog->accept(); OpenNow(path);
    });
    QObject::connect(ui.saveButton,&QPushButton::clicked,dialog,[this,dialog,sceneID,path] {
        if (SceneID()!=sceneID) { dialog->reject(); return; }
        if (_Scenes->GetActiveScenePath().empty()) {
            dialog->accept();
            ShowSaveDialog([this,path] { OpenNow(path); });
            return;
        }
        std::string error;
        if (!_Scenes->SaveScene(error)) { ShowError(error); return; }
        dialog->accept(); OpenNow(path);
    });
    dialog->open();
}
void SceneEditorUI::ShowError(const std::string& message) {
    if (message.empty() || !_Dispatch) return;
    if (_ErrorDialog) { _ErrorDialog->raise(); return; }
    QDialog* dialog = new QDialog();
    Ui::SceneErrorDialog ui;
    ui.setupUi(dialog);
    ui.messageLabel->setText(QString::fromStdString(message));
    _ErrorDialog = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::ApplicationModal);
    QObject::connect(dialog,&QObject::destroyed,_Dispatch,[this,dialog] { if (_ErrorDialog==dialog) _ErrorDialog=nullptr; });
    QObject::connect(dialog,&QDialog::finished,_Dispatch,[this,dialog] { if (_ErrorDialog==dialog) _ErrorDialog=nullptr; });
    QObject::connect(ui.buttonBox,&QDialogButtonBox::accepted,dialog,&QDialog::accept);
    dialog->open();
}
