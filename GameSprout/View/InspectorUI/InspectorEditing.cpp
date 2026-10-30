#include "InspectorUI.h"
#include "ImportSettingsInspector.h"
#include "ui_InspectorUI.h"
#include "ui_InspectorPendingChanges.h"
#include "../../Module/Shared/AssetArtifactIO.h"
#include <QDialog>

bool InspectorUI::HasPendingChanges() const {
    if (!_Window || !_Editable || !_ActivePanel) return false;
    Json::Value draft;
    std::string error;
    return !_ActivePanel->Read(draft,error) || AssetArtifactIO::JsonText(draft)!=_BaselineSettings;
}
void InspectorUI::UpdateButtons() {
    if (!_Window) return;
    const bool modified=HasPendingChanges();
    Json::Value draft;
    std::string error;
    const bool valid=!_Editable || (_ActivePanel && _ActivePanel->Read(draft,error));
    _Ui->applyButton->setEnabled(modified && valid);
    _Ui->revertButton->setEnabled(modified || !_OperationError.isEmpty());
    QString message=_OperationError;
    if (!valid) message=QString::fromStdString(error);
    else if (message.isEmpty() && modified) message=QStringLiteral("有未应用的导入设置。");
    _Ui->messageLabel->setText(message);
    _Ui->messageLabel->setVisible(!message.isEmpty());
}
bool InspectorUI::ApplyChanges() {
    if (!_Editable || !_Window || _Inspected.size()!=1) return false;
    Json::Value draft;
    std::string error;
    if (!_ActivePanel->Read(draft,error) || !_Assets->ApplyImportSettings(Guid(_Inspected.front().GuidString),
        AssetArtifactIO::JsonText(draft),_BaselineSettings,error)) {
        _OperationError=QString::fromStdString(error);
        UpdateButtons();
        return false;
    }
    // Use the committed representation (e.g. float32 normalization), so an
    // immediate second edit compares against the settings actually saved.
    std::string snapshot;
    Json::Value committed;
    IAssetMeta * meta=_Assets->QueryAsset(Guid(_Inspected.front().GuidString));
    if (meta && meta->SerializeMeta(snapshot) && AssetArtifactIO::Parse(snapshot,committed)) draft=committed["ImportSettings"];
    _BaselineSettings=AssetArtifactIO::JsonText(draft);
    _ActivePanel->Load(draft);
    _OperationError.clear();_Dirty=true;
    UpdateButtons();
    return true;
}
void InspectorUI::RevertChanges() {
    if (!_Window || _Inspected.size()!=1) return;
    IAssetMeta * meta=_Assets->QueryAsset(Guid(_Inspected.front().GuidString));
    std::string snapshot;
    Json::Value value;
    if (!meta || !meta->SerializeMeta(snapshot) || !AssetArtifactIO::Parse(snapshot,value)) return;
    _BaselineSettings=AssetArtifactIO::JsonText(value["ImportSettings"]);
    if (_Editable && _ActivePanel) _ActivePanel->Load(value["ImportSettings"]);
    _OperationError.clear();_Dirty=true;
    UpdateButtons();
}
bool InspectorUI::ResolvePendingChanges() {
    if (!HasPendingChanges()) return true;
    _ResolvingSelection=true;
    struct Guard { bool & Value;~Guard(){Value=false;} } guard{_ResolvingSelection};
    QDialog dialog(_Window);
    Ui::InspectorPendingChangesDialog form;
    form.setupUi(&dialog);
    QObject::connect(form.applyButton,&QPushButton::clicked,&dialog,[&] { if (ApplyChanges()) dialog.accept(); });
    QObject::connect(form.discardButton,&QPushButton::clicked,&dialog,[&] { RevertChanges();dialog.accept(); });
    QObject::connect(form.cancelButton,&QPushButton::clicked,&dialog,&QDialog::reject);
    return dialog.exec()==QDialog::Accepted;
}
