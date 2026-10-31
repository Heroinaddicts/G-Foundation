#include "AnimationResourceSnapshot.h"
#include "ILibraryManager.h"
#include "IAssetsManager.h"
#include "../Shared/AnimationDefinition.h"

void CaptureAnimationResources(AssetImportRequest& request,IAssetsManager* assets,ILibraryManager* library) {
    if(!assets||!library||(request.Importer!=eAssetImporterType::AnimatorController&&request.Importer!=eAssetImporterType::AvatarMask&&request.Importer!=eAssetImporterType::AnimationClip))return;
    const QDir root(QString::fromStdString(request.AssetsRoot));
    std::set<std::string> documents;
    std::function<void(const Json::Value&)> capture;
    std::function<void(const GameSproutRuntime::ResourceReference&)> resource;
    std::function<void(const std::string&)> document;
    document=[&](const std::string& relative) {
        if(!AssetArtifactIO::RelativePath(relative)||!documents.insert(relative).second)return;
        QFile file(root.filePath(QString::fromStdString(relative)));
        if(!file.open(QIODevice::ReadOnly)||file.size()>8*1024*1024)return;
        Json::Value value;if(AssetArtifactIO::Parse(file.readAll().toStdString(),value))capture(value);
    };
    resource=[&](const GameSproutRuntime::ResourceReference& reference) {
        const std::string key=reference.Guid+"_"+std::to_string(reference.LocalID);
        if(request.PreparedResources.count(key))return;
        const auto* dependency=assets->QueryAsset(Guid(reference.Guid));
        IAsset* asset=library->QueryAsset(Guid(reference.Guid),reference.LocalID);
        if(!asset||!dependency||dependency->GetImportState()==eAssetImportState::Failed)return;
        AssetImportPreparedResource value;value.Type=asset->GetAssetType();value.Path=asset->GetArtifactPath();
        value.DescriptorJson=asset->GetDataDescriptorJson();value.Size=asset->GetDataSize();value.Version=asset->GetDataVersion();
        AssetImportManifest manifest;
        if(library->GetManifest(Guid(reference.Guid),manifest))for(const auto& item:manifest.Assets)if(item.LocalID==reference.LocalID){value.Hash=item.Hash;break;}
        request.PreparedResources.emplace(key,std::move(value));
        if(asset->GetAssetType()==eAssetType::AvatarMask)document(dependency->GetPath());
        if(asset->GetAssetType()==eAssetType::AnimationClip) {
            Json::Value descriptor;if(AssetArtifactIO::Parse(asset->GetDataDescriptorJson(),descriptor)&&descriptor.isMember("SourceAvatar"))capture(descriptor["SourceAvatar"]);
        }
        if(asset->GetAssetType()==eAssetType::Avatar)for(const auto& item:manifest.Assets)if(item.Type==eAssetType::Skeleton)resource({reference.Guid,item.LocalID});
    };
    capture=[&](const Json::Value& value) {
        if(value.isObject()) {
            GameSproutRuntime::ResourceReference reference;
            if(AnimationDefinition::Reference(value,reference)) {
                request.DeclaredAnimationDependencies.insert(reference.Guid);
                const auto* dependency=assets->QueryAsset(Guid(reference.Guid));
                if(dependency&&reference.Guid!=request.GuidString) {
                    if(dependency->GetImportState()==eAssetImportState::Failed)request.FailedAnimationDependencies.insert(reference.Guid);
                    const auto importer=dependency->_ImporterType;
                    if(importer==eAssetImporterType::Model||(request.Importer==eAssetImporterType::AnimatorController
                        &&(importer==eAssetImporterType::AnimationClip||importer==eAssetImporterType::AvatarMask)))
                        request.AnimationDependencies.insert(reference.Guid);
                }
                resource(reference);
            }
            for(const auto& child:value)capture(child);
        } else if(value.isArray())for(const auto& child:value)capture(child);
    };
    if(request.Importer==eAssetImporterType::AnimationClip) {
        Json::Value meta;if(AssetArtifactIO::Parse(request.MetaJson,meta)&&meta["ImportSettings"].isMember("SourceAvatar"))capture(meta["ImportSettings"]["SourceAvatar"]);
    } else document(request.SourcePath);
}

void ResolveAnimationSnapshotMismatch(const AssetImportJob& job,IAssetsManager* assets,ILibraryManager* library,AssetImportResult& result) {
    if(!result.Superseded||!result.Retry||job.MismatchedPreparedResource.empty())return;
    const auto found=job.Request.PreparedResources.find(job.MismatchedPreparedResource);
    if(found==job.Request.PreparedResources.end())return;
    const auto separator=job.MismatchedPreparedResource.rfind('_');if(separator==std::string::npos)return;
    const Guid guid(job.MismatchedPreparedResource.substr(0,separator));
    const auto* meta=assets->QueryAsset(guid);
    if(meta&&meta->GetImportState()==eAssetImportState::Pending)return;
    const auto observed=job.ReadVersions.find(found->second.Path);AssetFileStamp current;
    if(observed!=job.ReadVersions.end()&&ReadAssetFileStamp(found->second.Path,current)&&current!=observed->second)return;
    AssetImportManifest manifest;
    if(library->GetManifest(guid,manifest))for(const auto& artifact:manifest.Assets) {
        if(job.MismatchedPreparedResource!=guid.GetGuidString()+"_"+std::to_string(artifact.LocalID))continue;
        // A publication changed after capture: a fresh request is meaningful.
        if(artifact.Hash!=found->second.Hash||artifact.DescriptorJson!=found->second.DescriptorJson)return;
        break;
    }
    // A Ready producer and an unchanged Library record cannot repair themselves
    // by retrying only the consumer. Stop Loading and identify the producer.
    result.Superseded=false;result.Retry=false;
    result.Error="Prepared animation dependency is damaged; reimport its source: "+(meta?meta->GetPath():guid.GetGuidString());
}
