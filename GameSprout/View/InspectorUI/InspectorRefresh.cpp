#include "InspectorUI.h"
#include "ImportSettingsInspector.h"
#include "AssetInfoInspector.h"
#include "GameObjectInspector.h"
#include "MaterialInspector.h"
#include "AnimationClipPreviewInspector.h"
#include "AvatarMaskInspector.h"
#include "AvatarInspector.h"
#include "AssetDisplayNames.h"
#include "ui_InspectorUI.h"
#include "../../Module/Shared/AssetArtifactIO.h"
#include <QFileInfo>

void InspectorUI::Clear(const QString & message) {
    _Editable=false;_ActivePanel=nullptr;_BaselineSettings.clear();_BaselineMaterialSource.clear();_OperationError.clear();_MaterialMode=false;
    if (_Material) { _Material->Deactivate();_Material->hide(); }
    _Ui->assetNameLabel->setText("Inspector");
    _Ui->assetNameLabel->setToolTip({});
    _Ui->typeLabel->clear();_Ui->stateLabel->clear();
    _Ui->pathEdit->clear();_Ui->guidEdit->clear();_Ui->localIDEdit->clear();
    _Ui->pathEditLabel->setText(QStringLiteral("路径"));
    _Ui->guidEditLabel->setText(QStringLiteral("GUID"));
    _Ui->localIDEditLabel->setText(QStringLiteral("LocalID"));
    if (_SceneInfo) _SceneInfo->Clear();
    _Ui->emptyLabel->setText(message);
    _Ui->contentStack->setCurrentIndex(0);
    UpdateButtons();
}

void InspectorUI::Refresh() {
    _Dirty=false;
    if(_ClipPreview)_ClipPreview->hide();
    if(_MaskInspector)_MaskInspector->hide();
    if(_AvatarInspector)_AvatarInspector->hide();
    if (_Ui->lockButton->isChecked() && _LockedSceneMissing) {
        Clear(QStringLiteral("锁定的节点已删除或场景已切换。解锁后可查看其他选择。"));
        return;
    }
    auto desired=_Selection->GetAssetSelection();
    SceneObjectSelection desiredScene=_Selection->GetSceneObjectSelection();
    if (_Ui->lockButton->isChecked()) {
        if (_InspectedScene.ObjectID) { desiredScene=_InspectedScene;desired.clear(); }
        else if (!_Inspected.empty()) { desired=_Inspected;desiredScene={}; }
    }
    const bool changed=desired!=_Inspected || desiredScene!=_InspectedScene;
    const bool previousExists=_Inspected.size()==1 && _Assets->QueryAsset(Guid(_Inspected.front().GuidString));
    if (changed && previousExists && !ResolvePendingChanges()) {
        _Selection->SetAssetSelection(_Inspected);
        return;
    }
    _Inspected=desired;
    _InspectedScene=desiredScene;
    if (changed) _OperationError.clear();
    if (desiredScene.ObjectID) {
        auto * object=ResolveSceneObject(desiredScene);
        if (!object) {
            _LockedSceneMissing=_Ui->lockButton->isChecked();
            _InspectedScene={};_Inspected.clear();
            Clear(_LockedSceneMissing ? QStringLiteral("锁定的节点已删除或场景已切换。解锁后可查看其他选择。") : QStringLiteral("节点已删除或场景已切换。"));
            return;
        }
        RefreshSceneObject(*object);
        return;
    }
    if (changed && _SceneInfo) _SceneInfo->Clear();
    if (desired.size()!=1) {
        Clear(desired.empty() ? QStringLiteral("选择一个资源或场景节点以查看属性。") : QStringLiteral("已选择 %1 项。当前版本请单选资源查看与编辑属性。").arg(desired.size()));
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
    _Ui->pathEditLabel->setText(QStringLiteral("路径"));
    _Ui->guidEditLabel->setText(QStringLiteral("GUID"));
    _Ui->localIDEditLabel->setText(QStringLiteral("LocalID"));
    _Ui->assetNameLabel->setText(QFileInfo(path).fileName());
    _Ui->assetNameLabel->setToolTip("Assets/"+path);
    _Ui->pathEdit->setText("Assets/"+path);
    _Ui->guidEdit->setText(QString::fromStdString(selected.GuidString));
    _Ui->localIDEdit->setText(selected.IsSubAsset ? QString::number(selected.LocalID) : QStringLiteral("源文件"));
    _Ui->typeLabel->setText(QString::fromStdString(value["ImporterType"].asString()));
    const auto state=meta->GetImportState();
    _Ui->stateLabel->setText(state==eAssetImportState::Ready ? "就绪" : state==eAssetImportState::Failed ? "导入失败" : state==eAssetImportState::Pending ? "待导入" : "无需导入");
    IAsset * selectedAsset=selected.IsSubAsset?_Library->QueryAsset(meta->_Guid,selected.LocalID):nullptr;
    if (_RuntimeAssets && selectedAsset && selectedAsset->GetAssetType()==eAssetType::AnimationClip) {
        RefreshAnimationClip(*meta,*selectedAsset);return;
    }
    if(_RuntimeAssets&&meta->_ImporterType==eAssetImporterType::AvatarMask){
        GameSproutRuntime::ResourceReference reference{selected.GuidString,selected.LocalID};
        if(!selected.IsSubAsset){AssetImportManifest manifest;if(_Library->GetManifest(meta->_Guid,manifest))for(const auto& asset:manifest.Assets)
            if(asset.Type==eAssetType::AvatarMask&&asset.SourceKey=="main"){reference.LocalID=asset.LocalID;break;}}
        _Editable=false;_ActivePanel=nullptr;_MaterialMode=false;_Material->Deactivate();_Material->hide();
        for(const auto& panel:_Panels)panel.second->hide();_BaselineSettings.clear();_BaselineMaterialSource.clear();
        _MaskInspector->Load(_RuntimeAssets,_Assets,_Library,_Selection,reference);_MaskInspector->Edit=[this,reference]{EditAvatarMask(reference);};
        _MaskInspector->show();_Ui->textureHost->show();_Ui->infoHost->hide();_Ui->contentStack->setCurrentIndex(1);UpdateButtons();return;
    }
    if(_RuntimeAssets&&selectedAsset&&selectedAsset->GetAssetType()==eAssetType::Avatar){
        const GameSproutRuntime::ResourceReference reference{selected.GuidString,selected.LocalID};
        _Editable=false;_ActivePanel=nullptr;_MaterialMode=false;_Material->Deactivate();_Material->hide();
        for(const auto& panel:_Panels)panel.second->hide();_BaselineSettings.clear();_BaselineMaterialSource.clear();
        _AvatarInspector->Load(_RuntimeAssets,reference);_AvatarInspector->Edit=[this,reference]{EditModelRig(reference);};
        _AvatarInspector->show();_Ui->textureHost->show();_Ui->infoHost->hide();_Ui->contentStack->setCurrentIndex(1);UpdateButtons();return;
    }
    if (meta->_ImporterType==eAssetImporterType::Material || (selectedAsset && selectedAsset->GetAssetType()==eAssetType::Material)) {
        RefreshMaterial(meta,selectedAsset?selectedAsset->GetLocalID():0,changed);
        return;
    }
    if (_MaterialMode) { _MaterialMode=false;_BaselineMaterialSource.clear();_Material->Deactivate(); }
    _Material->hide();
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
    BindAnimationImportSources(*meta,panel);
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
