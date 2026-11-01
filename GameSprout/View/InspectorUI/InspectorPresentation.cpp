#include "InspectorUI.h"
#include "AssetInfoInspector.h"
#include "ui_InspectorUI.h"
void InspectorUI::SetDebugMode(bool debug) { _DebugMode = debug; UpdatePresentation(); }
void InspectorUI::UpdatePresentation() {
    if (!_Window) return;
    const auto description = _ActivePanel ? _ActivePanel->Describe() : InspectorPresentation{};
    const bool target = _ActivePanel != nullptr;
    _Ui->assetHeader->setVisible(!description.SceneObject);
    _Ui->assetNameLabel->setText(target ? description.Name : QStringLiteral("Inspector"));
    _Ui->assetNameLabel->setToolTip(description.Path);
    _Ui->typeLabel->setText(description.Type); _Ui->stateLabel->setText(description.State);
    _Ui->pathEditLabel->setText(description.SceneObject ? QStringLiteral("节点路径") : QStringLiteral("路径"));
    _Ui->guidEditLabel->setText(description.SceneObject ? QStringLiteral("场景 ID") : QStringLiteral("GUID"));
    _Ui->localIDEditLabel->setText(description.SceneObject ? QStringLiteral("节点 ID") : QStringLiteral("LocalID"));
    _Ui->pathEdit->setText(description.Path); _Ui->pathEdit->setToolTip(description.Path);
    _Ui->guidEdit->setText(description.Guid); _Ui->localIDEdit->setText(description.LocalID);
    _Ui->diagnosticsHost->setVisible(_DebugMode && target);
    _Ui->infoHost->setVisible(_DebugMode && target && !description.SceneObject);
    if (_Info) _Info->Load(QStringLiteral("资源诊断信息"), description.Diagnostics);
    if (_ActivePanel) { _ActivePanel->SetDebugMode(_DebugMode); _ActivePanel->SetLocked(_Ui->lockButton->isChecked()); }
    if (_NormalModeMenu) _NormalModeMenu->SetEnabled(_DebugMode);
    if (_DebugModeMenu) _DebugModeMenu->SetEnabled(!_DebugMode);
}
