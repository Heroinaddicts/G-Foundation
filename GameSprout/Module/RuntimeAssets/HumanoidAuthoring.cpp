#include "RuntimeAssets.h"
#include "LibraryRuntimeResources.h"
#include "ILibraryManager.h"
#include "IProject.h"
#include "../Shared/HumanoidDefinition.h"
#include "../Shared/AssetArtifactIO.h"

bool RuntimeAssets::LoadModelRigSource(const GameSproutRuntime::ResourceReference& reference,
    ModelRigImportSettings& output,GameSproutRuntime::SkeletonData& skeleton,std::string& snapshot,std::string& error) const {
    output={};skeleton={};snapshot.clear();error.clear();
    if(!_Assets||!_Library||!_Resources){error="资源服务未初始化。";return false;}
    const Guid guid(reference.Guid);IAssetMeta* meta=_Assets->QueryAsset(guid);AssetImportManifest manifest;
    if(!guid.IsValid()||!meta||meta->_ImporterType!=eAssetImporterType::Model||!_Library->GetManifest(guid,manifest)) {
        error="请选择已导入模型或其 Avatar 子资源。";return false;
    }
    bool selected=false;GameSproutRuntime::ResourceReference skeletonReference;
    for(const auto& asset:manifest.Assets) {
        if(asset.LocalID==reference.LocalID&&(asset.Type==eAssetType::Model||asset.Type==eAssetType::Avatar))selected=true;
        if(asset.Type==eAssetType::Skeleton&&asset.SourceKey=="skeleton")skeletonReference={reference.Guid,asset.LocalID};
    }
    if(!selected||skeletonReference.Guid.empty()){error="该模型没有可配置的骨架。";return false;}
    std::string text;Json::Value document;
    if(!meta->SerializeMeta(text)||!AssetArtifactIO::Parse(text,document)
        ||!HumanoidDefinition::ReadSettings(document["ImportSettings"],output,error)
        ||!_Resources->LoadSkeleton(skeletonReference,skeleton,error))return false;
    const auto type=document["ImportSettings"].get("AnimationType","Generic").asString();
    if(type=="Generic")output.AnimationType=eModelAnimationType::Generic;
    else if(type=="Humanoid")output.AnimationType=eModelAnimationType::Humanoid;
    else if(type=="None")output.AnimationType=eModelAnimationType::None;
    else {error="模型的动画类型无效。";return false;}
    snapshot=AssetArtifactIO::JsonText(document["ImportSettings"]);return true;
}

bool RuntimeAssets::SaveModelRigSource(const GameSproutRuntime::ResourceReference& reference,
    const ModelRigImportSettings& settings,const std::string& expectedSnapshot,std::string& error) {
    ModelRigImportSettings current;GameSproutRuntime::SkeletonData skeleton;std::string snapshot;
    if(!LoadModelRigSource(reference,current,skeleton,snapshot,error))return false;
    Json::Value expected,source;
    if(!AssetArtifactIO::Parse(expectedSnapshot,expected)||!AssetArtifactIO::Parse(snapshot,source)||expected!=source) {
        error="模型导入设置已被外部修改，请重新加载。";return false;
    }
    if(settings.AnimationType!=eModelAnimationType::None&&settings.AnimationType!=eModelAnimationType::Generic&&settings.AnimationType!=eModelAnimationType::Humanoid) {
        error="未知的模型动画类型。";return false;
    }
    const auto human=HumanoidDefinition::SettingsJson(settings);ModelHumanoidImportSettings verified;
    if(!HumanoidDefinition::ReadSettings(human,verified,error))return false;
    if(settings.AnimationType==eModelAnimationType::Humanoid) {
        AssetImportManifest manifest;GameSproutRuntime::ResourceReference skeletonReference;
        if(!_Library->GetManifest(Guid(reference.Guid),manifest)){error="模型尚无骨架产物。";return false;}
        for(const auto& asset:manifest.Assets)if(asset.Type==eAssetType::Skeleton&&asset.SourceKey=="skeleton")skeletonReference={reference.Guid,asset.LocalID};
        GameSproutRuntime::HumanoidAvatarData avatar;
        if(!HumanoidDefinition::Prepare(verified,skeleton,skeletonReference,avatar,error))return false;
    }
    for(const auto& name:human.getMemberNames())source[name]=human[name];
    source["AnimationType"]=settings.AnimationType==eModelAnimationType::Humanoid?"Humanoid":settings.AnimationType==eModelAnimationType::None?"None":"Generic";
    return _Assets->ApplyImportSettings(Guid(reference.Guid),AssetArtifactIO::JsonText(source),snapshot,error);
}
