#include "AnimationResourceValidation.h"
#include "../Shared/AvatarMaskDefinition.h"
#include "../Shared/AssetValueBinary.h"

bool ValidateAnimationReference(AssetImportJob& job,const GameSproutRuntime::ResourceReference& ref,eAssetType expected) {
    if(job.Request.FailedAnimationDependencies.count(ref.Guid))return job.Fail("Animation dependency import failed: "+ref.Guid);
    const auto source=job.Request.SourcePaths.find(ref.Guid);
    if(source==job.Request.SourcePaths.end()||ref.Guid==job.Request.GuidString)return job.Fail("Animation dependency is missing or cyclic: "+ref.Guid);
    if(!job.AddDependency(source->second))return false;
    const QString path=QDir(QString::fromStdString(job.Request.AssetsRoot)).filePath(QString::fromStdString(source->second)+".meta");
    QByteArray bytes;std::string hash;Json::Value meta;
    if(!job.ReadFile(path.toStdString(),&bytes,hash)||!AssetArtifactIO::Parse(bytes.toStdString(),meta)
        ||meta["Guid"]!=ref.Guid||!meta["SubAssets"].isArray())return job.Fail("Cannot verify animation dependency Meta");
    for(const auto& identity:meta["SubAssets"])if(identity["LocalID"].isUInt64()&&identity["LocalID"].asUInt64()==ref.LocalID
        &&identity["Type"].isUInt()&&identity["Type"].asUInt()==static_cast<unsigned>(expected))return true;
    return job.Fail("Animation reference has a missing or wrong resource type: "+ref.Guid+"_"+std::to_string(ref.LocalID));
}
bool ReadAnimationPrepared(AssetImportJob& job,const GameSproutRuntime::ResourceReference& ref,eAssetType expected,Json::Value& descriptor,QByteArray& bytes) {
    if(!ValidateAnimationReference(job,ref,expected))return false;
    const auto found=job.Request.PreparedResources.find(ref.Guid+"_"+std::to_string(ref.LocalID));
    if(found==job.Request.PreparedResources.end()||found->second.Type!=expected||found->second.Version!=2||found->second.Hash.empty()
        ||!AssetArtifactIO::Parse(found->second.DescriptorJson,descriptor)||!descriptor.isObject())return job.Fail("Animation dependency has no current prepared resource; import its source first");
    std::string hash;
    if(!job.ReadFile(found->second.Path,&bytes,hash)||static_cast<uint64_t>(bytes.size())!=found->second.Size)return job.Fail("Animation dependency prepared resource is missing or damaged");
    if(hash!=found->second.Hash) {
        job.MismatchedPreparedResource=ref.Guid+"_"+std::to_string(ref.LocalID);
        job.Result.Superseded=true;job.Result.Retry=true;
        return job.Fail("Animation dependency artifact changed before worker validation");
    }
    return true;
}
bool ValidateMaskSkeleton(AssetImportJob& job,const GameSproutRuntime::AvatarMaskData& mask,GameSproutRuntime::SkeletonData& skeleton) {
    Json::Value descriptor,body;QByteArray bytes;
    if(!ReadAnimationPrepared(job,mask.Skeleton,eAssetType::Skeleton,descriptor,bytes))return false;
    if(descriptor["Encoding"]!="BinaryValueLE1"||!AssetValueBinary::Decode(bytes,body)||!body["Nodes"].isArray())return job.Fail("Invalid prepared Mask Skeleton");
    for(const auto& value:body["Nodes"]) {
        if(!value["Path"].isString()||!value["Parent"].isInt())return job.Fail("Invalid prepared Skeleton node");
        GameSproutRuntime::SkeletonNode node;node.Path=value["Path"].asString();node.Parent=value["Parent"].asInt();skeleton.Nodes.push_back(node);
    }
    std::string error;if(!AvatarMaskDefinition::Validate(mask,skeleton,error))return job.Fail(error);
    return true;
}

bool AnimationCachedInputsCurrent(AssetImportJob& job) {
    if(job.Request.Importer!=eAssetImporterType::AnimatorController&&job.Request.Importer!=eAssetImporterType::AvatarMask&&job.Request.Importer!=eAssetImporterType::AnimationClip)return true;
    Json::Value validation;
    if(!AssetArtifactIO::Parse(job.Request.AnimationValidationJson,validation)||validation["Version"]!=2
        ||validation["Generation"]!=job.Request.Previous.Generation||!validation["PreparedInputs"].isArray())return false;
    std::set<std::string> seen;
    for(const auto& input:validation["PreparedInputs"]) {
        if(!input["Guid"].isString()||!input["LocalID"].isUInt64()||!input["Path"].isString()
            ||!AssetArtifactIO::RelativePath(input["Path"].asString())||!input["Hash"].isString()||!input["DescriptorHash"].isString())return false;
        const auto key=input["Guid"].asString()+"_"+std::to_string(input["LocalID"].asUInt64());
        if(!seen.insert(key).second)return false;
        const auto found=job.Request.PreparedResources.find(key);
        if(found==job.Request.PreparedResources.end())return false;
        const auto& resource=found->second;
        const auto path=QDir(QString::fromStdString(job.Request.LibraryRoot)).filePath(QString::fromStdString(input["Path"].asString())).toStdString();
        if(resource.Path!=path||input["Hash"]!=resource.Hash||input["DescriptorHash"]!=AssetArtifactIO::Hash(QByteArray::fromStdString(resource.DescriptorJson)))return false;
        std::string hash;
        if(!job.ReadFile(path,nullptr,hash)||hash!=resource.Hash)return false;
    }
    return true;
}
