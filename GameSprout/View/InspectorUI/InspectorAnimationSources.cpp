#include "InspectorUI.h"
#include "ModelInspector.h"
#include "AnimationClipInspector.h"
#include "AnimationClipPreviewInspector.h"
#include "MaterialInspector.h"
#include "AssetInfoInspector.h"
#include "ui_InspectorUI.h"
#include "IRuntimeAssets.h"
#include <algorithm>

// Lists come from prepared resource services; this View never reads FBX/.anim files.
void InspectorUI::BindAnimationImportSources(IAssetMeta& meta,ImportSettingsInspector* panel) {
    auto* model=dynamic_cast<ModelInspector*>(panel);
    auto* clip=dynamic_cast<AnimationClipInspector*>(panel);
    if(!model&&!clip)return;
    if(clip)clip->BindSourceAvatar(_Assets,_Library,_Selection,"AnimationSourceAvatar:"+meta._Guid.GetGuidString());
    std::vector<std::string> names,paths;
    if(model)for(auto* asset:meta.GetSubAssets())if(asset&&asset->GetAssetType()==eAssetType::AnimationClip)names.push_back(asset->GetName());
    auto* provider=_RuntimeAssets?_RuntimeAssets->GetResourceProvider():nullptr;
    AssetImportManifest manifest;
    if(provider&&_Library&&_Library->GetManifest(meta._Guid,manifest)) {
        for(const auto& asset:manifest.Assets) {
            const GameSproutRuntime::ResourceReference reference{meta._Guid.GetGuidString(),asset.LocalID};
            std::string error;
            if(model&&asset.Type==eAssetType::Model&&asset.SourceKey=="main") {
                GameSproutRuntime::ModelData data;
                model->ConfigureRig=[this,reference]{if(ResolvePendingChanges())EditModelRig(reference);};
                if(provider->LoadModel(reference,data,error))for(const auto& node:data.Nodes)paths.push_back(node.Path);
            } else if(clip&&asset.Type==eAssetType::AnimationClip&&asset.SourceKey=="main") {
                GameSproutRuntime::AnimationClipData data;
                if(provider->LoadAnimationClip(reference,data,error))paths=data.Tracks;
            }
        }
    }
    std::sort(names.begin(),names.end());names.erase(std::unique(names.begin(),names.end()),names.end());
    if(model)model->SetAnimationSources(names,paths);
    if(clip)clip->SetNodePaths(paths);
}

void InspectorUI::RefreshAnimationClip(IAssetMeta& meta,IAsset& asset) {
    GameSproutRuntime::AnimationClipData data;std::string error;
    auto* provider=_RuntimeAssets?_RuntimeAssets->GetResourceProvider():nullptr;
    if(!provider||!provider->LoadAnimationClip({meta._Guid.GetGuidString(),asset.GetLocalID()},data,error)) {
        Clear(QStringLiteral("无法读取动画片段：")+QString::fromStdString(error));return;
    }
    _MaterialMode=false;_Material->Deactivate();_Material->hide();
    _Editable=false;_ActivePanel=nullptr;_BaselineSettings.clear();_BaselineMaterialSource.clear();_OperationError.clear();
    for(const auto& entry:_Panels)entry.second->hide();
    _ClipPreview->Load(data);const auto guid=meta._Guid.GetGuidString();
    _ClipPreview->EditSource=[this,guid]{_Ui->lockButton->setChecked(false);_Selection->SetAssetSelection({{guid,0,false}});_Dirty=true;};
    _ClipPreview->show();_Ui->textureHost->show();_Ui->infoHost->hide();
    _Ui->assetNameLabel->setText(QString::fromStdString(asset.GetName()));_Ui->typeLabel->setText(QStringLiteral("AnimationClip · 已导入片段"));
    Json::Value info(Json::objectValue);info["ArtifactPath"]=asset.GetArtifactPath();info["DataVersion"]=asset.GetDataVersion();info["DataSize"]=Json::UInt64(asset.GetDataSize());
    _Info->Load(QStringLiteral("动画产物信息"),info);
    _Ui->contentStack->setCurrentIndex(1);UpdateButtons();_Ui->applyButton->hide();_Ui->revertButton->hide();
}
