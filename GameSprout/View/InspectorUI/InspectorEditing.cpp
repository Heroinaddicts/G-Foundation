#include "InspectorUI.h"
#include "ui_InspectorUI.h"
#include "ui_InspectorPendingChanges.h"
#include <QDialog>
bool InspectorUI::HasPendingChanges() const { return _ActivePanel && _ActivePanel->HasPendingChanges(); }
void InspectorUI::UpdateButtons() {
    if (!_Window) return;
    UpdatePresentation();
    const auto description = _ActivePanel ? _ActivePanel->Describe() : InspectorPresentation{};
    const bool editable = _ActivePanel && description.Editable;
    const bool pending = HasPendingChanges(); std::string error;
    const bool valid = !_ActivePanel || _ActivePanel->Validate(error);
    _Ui->applyButton->setText(description.ApplyText);
    _Ui->applyButton->setVisible(editable); _Ui->revertButton->setVisible(editable);
    _Ui->applyButton->setEnabled(editable && pending && valid);
    _Ui->revertButton->setEnabled(editable && (pending || !_OperationError.isEmpty()));
    QString message = valid ? _OperationError : QString::fromStdString(error);
    if (message.isEmpty() && pending) message = QStringLiteral("有未保存的修改。");
    _Ui->messageLabel->setText(message); _Ui->messageLabel->setVisible(!message.isEmpty());
}
bool InspectorUI::ApplyChanges() {
    if (!_ActivePanel) return false;
    std::string error; const bool success = _ActivePanel->Apply(error);
    _OperationError = QString::fromStdString(error); UpdateButtons(); if (success) RequestRefresh(); return success;
}
void InspectorUI::RevertChanges() {
    if (!_ActivePanel) return;
    std::string error; _ActivePanel->Revert(error); _OperationError = QString::fromStdString(error); UpdateButtons(); RequestRefresh();
}
bool InspectorUI::ResolvePendingChanges() {
    if (!HasPendingChanges()) return true;
    _ResolvingSelection = true;
    QDialog dialog(_Window); Ui::InspectorPendingChangesDialog form; form.setupUi(&dialog);
    QObject::connect(form.applyButton, &QPushButton::clicked, &dialog, [&] { if (ApplyChanges()) dialog.accept(); });
    QObject::connect(form.discardButton, &QPushButton::clicked, &dialog, [&] { RevertChanges(); dialog.accept(); });
    QObject::connect(form.cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    _ResolvingSelection = false;
    if (_RefreshDeferred) { _RefreshDeferred = false; RequestRefresh(); }
    return accepted;
}
