#include "PrefabStageUI.h"
#include "ui_PrefabStageCloseDialog.h"
#include "ui_PrefabStageErrorDialog.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>

void PrefabStageUI::RequestClose(const std::function<void()>& afterClose) {
    QueueNavigation([this,afterClose]{CloseNow(afterClose);});
}
void PrefabStageUI::CloseNow(const std::function<void()>& afterClose) {
    if(!_Stage||!_CanEdit)return;
    if(QWidget* focused=QApplication::focusWidget())focused->clearFocus();
    auto state=_Stage->GetState();
    if(!state.Active){if(afterClose)afterClose();return;}
    if(_CloseDialog){_CloseDialog->raise();return;}
    // Close validates live content. A cached clean state can become dirty when
    // field focus is committed or the document is captured for this request.
    std::string closeError;
    if(_Stage->Close(false,closeError)){if(afterClose)afterClose();return;}
    state=_Stage->GetState();
    if(!state.Active||!state.Dirty){ShowError(closeError);return;}
    auto* dialog=new QDialog(QApplication::activeWindow());_CloseDialog=dialog;_DialogGuid=state.Guid;
    dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowModality(Qt::ApplicationModal);
    QObject::connect(dialog,&QDialog::finished,_Dispatch,[this,dialog]{if(_CloseDialog==dialog){_CloseDialog=nullptr;_DialogGuid.clear();}});
    Ui::PrefabStageCloseDialog form;form.setupUi(dialog);
    form.messageLabel->setText(QStringLiteral("Prefab“%1”有未保存修改。保存后会更新场景实例并保留各自的 Overrides。").arg(QString::fromStdString(state.Path)));
    QObject::connect(form.cancelButton,&QPushButton::clicked,dialog,&QDialog::reject);
    const auto finish=[this,guard=QPointer<QDialog>(dialog),form,guid=state.Guid,sceneID=state.SceneID,afterClose](bool save) {
        QueueNavigation([this,guard,form,guid,sceneID,afterClose,save] {
            if(!guard||!guard->isVisible()||_CloseDialog!=guard||!_Stage||!_CanEdit)return;
            const auto current=_Stage->GetState();
            if(!current.Active||current.Guid!=guid||current.SceneID!=sceneID){guard->reject();return;}
            std::string error;
            if(save&&!_Stage->Save(error)){form.errorLabel->setText(QString::fromStdString(error));return;}
            if(!_Stage->Close(!save,error)){form.errorLabel->setText(QString::fromStdString(error));return;}
            if(guard)guard->accept();if(afterClose)afterClose();
        });
    };
    QObject::connect(form.saveButton,&QPushButton::clicked,dialog,[finish]{finish(true);});
    QObject::connect(form.discardButton,&QPushButton::clicked,dialog,[finish]{finish(false);});
    dialog->open();
}
void PrefabStageUI::ShowError(const std::string& error) {
    if(error.empty()||!_Dispatch)return;
    delete _ErrorDialog.data();
    auto* dialog=new QDialog(QApplication::activeWindow());_ErrorDialog=dialog;dialog->setAttribute(Qt::WA_DeleteOnClose);
    Ui::PrefabStageErrorDialog form;form.setupUi(dialog);form.messageLabel->setText(QString::fromStdString(error));
    QObject::connect(form.buttonBox,&QDialogButtonBox::accepted,dialog,&QDialog::accept);dialog->open();
}
