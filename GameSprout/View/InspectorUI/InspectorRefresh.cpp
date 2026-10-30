#include "InspectorUI.h"
#include "ImportSettingsInspector.h"
#include "AssetInfoInspector.h"
#include "AssetDisplayNames.h"
#include "ui_InspectorUI.h"
#include "../../Module/Shared/AssetArtifactIO.h"
#include <QFileInfo>

void InspectorUI::Clear(const QString & message) {
    _Editable=false;_ActivePanel=nullptr;_BaselineSettings.clear();_OperationError.clear();
    _Ui->assetNameLabel->setText("Inspector");
    _Ui->typeLabel->clear();_Ui->stateLabel->clear();
    _Ui->pathEdit->clear();_Ui->guidEdit->clear();_Ui->localIDEdit->clear();
    _Ui->emptyLabel->setText(message);
    _Ui->contentStack->setCurrentIndex(0);
    UpdateButtons();
}

void InspectorUI::Refresh() {
    _Dirty=false;
    auto desired=_Ui->lockButton->isChecked() && !_Inspected.empty() ? _Inspected : _Selection->GetAssetSelection();
    const bool changed=desired!=_Inspected;
    const bool previousExists=_Inspected.size()==1 && _Assets->QueryAsset(Guid(_Inspected.front().GuidString));
    if (changed && previousExists && !ResolvePendingChanges()) {
        _Selection->SetAssetSelection(_Inspected);
        return;
    }
    _Inspected=desired;
    if (changed) _OperationError.clear();
    if (desired.size()!=1) {
        Clear(desired.empty() ? QStringLiteral("选择一个资源以查看属性。") : QStringLiteral("已选择 %1 项。当前版本请单选资源查看与编辑属性。").arg(desired.size()));
        return;
    }
    const auto & selected=desired.front();
    IAssetMeta * meta=_Assets->QueryAsset(Guid(selected.GuidString));
    if (!meta) { Clear(QStringLiteral("资源已删除或尚未完成登记。"));return; }
    std::string snapshot;
    Json::Value value;
    if (!meta->SerializeMeta(snapshot) || !AssetArtifactIO::Parse(snapshot,value)) {
        Clear(QStringLiteral("无法读取资源设置。"));return;
    }
    const QString path=QString::fromStdString(meta->GetPath());
    _Ui->assetNameLabel->setText(QFileInfo(path).fileName());
    _Ui->pathEdit->setText("Assets/"+path);
    _Ui->guidEdit->setText(QString::fromStdString(selected.GuidString));
    _Ui->localIDEdit->setText(selected.IsSubAsset ? QString::number(selected.LocalID) : QStringLiteral("源文件"));
    _Ui->typeLabel->setText(QString::fromStdString(value["ImporterType"].asString()));
    const auto state=meta->GetImportState();
    _Ui->stateLabel->setText(state==eAssetImportState::Ready ? "就绪" : state==eAssetImportState::Failed ? "导入失败" : state==eAssetImportState::Pending ? "待导入" : "无需导入");
    Json::Value info(Json::objectValue);
    const auto found=_Panels.find(meta->_ImporterType);
    ImportSettingsInspector * panel=(!selected.IsSubAsset && found!=_Panels.end()) ? found->second : nullptr;
    if (selected.IsSubAsset) {
        IAsset * asset=_Library->QueryAsset(meta->_Guid,selected.LocalID);
        if (!asset) { Clear(QStringLiteral("此子资源已不存在，请重新选择。"));return; }
        _Ui->assetNameLabel->setText(QString::fromStdString(asset->GetName()));
        _Ui->typeLabel->setText(AssetDisplayName(asset->GetAssetType()));
        AssetImportManifest manifest;
        if (_Library->GetManifest(meta->_Guid,manifest)) for (const auto & child:manifest.Assets) if (child.LocalID==selected.LocalID) {
            info["Name"]=child.Name;
            info["LocalID"]=static_cast<Json::UInt64>(child.LocalID);
            info["SourceKey"]=child.SourceKey;
            info["Hash"]=child.Hash;
            break;
        }
        info["ArtifactPath"]=asset->GetArtifactPath();
        info["DataVersion"]=asset->GetDataVersion();
        info["DataSize"]=Json::UInt64(asset->GetDataSize());
        _Info->Load(QStringLiteral("已导入的子资源信息（只读）"),info);
    } else {
        info["子资源数量"]=static_cast<Json::UInt64>(meta->GetSubAssets().size());
        if (!panel) info["ImportSettings"]=value["ImportSettings"];
        _Info->Load(panel ? QStringLiteral("资源信息") : QStringLiteral("资源信息与导入设置（当前为只读）"),info);
    }
    const std::string current=AssetArtifactIO::JsonText(value["ImportSettings"]);
    if (panel && (changed || panel!=_ActivePanel || !HasPendingChanges())) {
        _BaselineSettings=current;
        panel->Load(value["ImportSettings"]);
        _OperationError.clear();
    } else if (panel && current!=_BaselineSettings) {
        _OperationError=QStringLiteral("设置已被外部修改。草稿仍保留，请点击恢复读取最新设置。");
    }
    _ActivePanel=panel;
    _Editable=panel!=nullptr;
    for (const auto & entry:_Panels) entry.second->setVisible(entry.second==panel);
    _Ui->textureHost->setVisible(panel!=nullptr);
    _Ui->infoHost->show();
    _Ui->contentStack->setCurrentIndex(1);
    if (!meta->GetImportError().empty()) _OperationError=QString::fromStdString(meta->GetImportError());
    UpdateButtons();
}
