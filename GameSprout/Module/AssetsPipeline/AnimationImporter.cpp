#include "AssetImportJob.h"
#include "AnimationClipPreparation.h"
#include "AnimationResourceValidation.h"
#include "../Shared/AvatarMaskDefinition.h"
#include "../Shared/AnimationImportSettingsJson.h"
#include "../Shared/AnimationDefinition.h"
#include "../Shared/HumanoidDefinition.h"
#include "../Shared/AssetValueBinary.h"
#include <QDataStream>
#include <QtEndian>
#include <cstring>

namespace {
bool FloatArray(const Json::Value& value,QDataStream& stream,unsigned count){
    if(!value.isArray()||value.size()!=count)return false;
    for(const auto& number:value){if(!number.isNumeric()||!std::isfinite(number.asDouble())||std::fabs(number.asDouble())>std::numeric_limits<float>::max())return false;stream<<number.asFloat();}
    return true;
}
}
bool ImportAnimationDocument(AssetImportJob& job,eAssetType type,const Json::Value& source){
    const auto name=QFileInfo(QString::fromStdString(job.Request.SourcePath)).completeBaseName().toStdString();
    if(type==eAssetType::AnimatorController){
        GameSproutRuntime::AnimatorControllerData controller;std::string error;
        if(!AnimationDefinition::ReadController(source,false,controller,error))return job.Fail(error);
        GameSproutRuntime::ResourceReference controllerSkeleton;
        for(const auto& layer:controller.Layers) {
            GameSproutRuntime::SkeletonData skeleton;
            if(!layer.Mask.Guid.empty()) {
                if(!ValidateAnimationReference(job,layer.Mask,eAssetType::AvatarMask))return false;
                const auto sourcePath=job.Request.SourcePaths.at(layer.Mask.Guid);
                QByteArray maskBytes;if(!job.AddDependency(sourcePath,&maskBytes))return false;
                Json::Value maskDocument;GameSproutRuntime::AvatarMaskData mask;
                if(!AssetArtifactIO::Parse(maskBytes.toStdString(),maskDocument)||!AvatarMaskDefinition::Read(maskDocument,false,mask,error))return job.Fail(error.empty()?"Invalid AvatarMask source":error);
                if(mask.Mode==GameSproutRuntime::AvatarMaskMode::Transform&&!ValidateMaskSkeleton(job,mask,skeleton))return false;
                if(!controllerSkeleton.Guid.empty()&&!mask.Skeleton.Guid.empty()&&controllerSkeleton!=mask.Skeleton)return job.Fail("Controller layer masks must reference the same Skeleton");
                if(!mask.Skeleton.Guid.empty())controllerSkeleton=mask.Skeleton;
            }
            std::vector<GameSproutRuntime::ResourceReference> clips;
            for(const auto& state:layer.States) {
                if(state.BlendParameter.empty())clips.push_back(state.Clip);
                else for(const auto& motion:state.Motions)clips.push_back(motion.Clip);
            }
            if(!layer.ReferenceClip.Guid.empty())clips.push_back(layer.ReferenceClip);
            for(const auto& motion:layer.MotionOverrides) {if(motion.BlendParameter.empty())clips.push_back(motion.Clip);else for(const auto& child:motion.Motions)clips.push_back(child.Clip);}
            std::set<std::string> paths;for(const auto& node:skeleton.Nodes)paths.insert(node.Path);
            for(const auto& clip:clips) {
                if(!ValidateAnimationReference(job,clip,eAssetType::AnimationClip))return false;
                if(paths.empty()&&clip!=layer.ReferenceClip)continue;
                Json::Value descriptor;QByteArray bytes;
                if(!ReadAnimationPrepared(job,clip,eAssetType::AnimationClip,descriptor,bytes))return false;
                if(descriptor["Format"]!="AnimationClip2"||descriptor["Encoding"]!="RawBytes"||!descriptor["Tracks"].isArray()
                    ||!descriptor["Duration"].isNumeric())return job.Fail("Invalid prepared layer AnimationClip");
                if(descriptor.isMember("SourceAvatar")) {
                    GameSproutRuntime::ResourceReference avatar;
                    if(!AnimationDefinition::Reference(descriptor["SourceAvatar"],avatar)||!ValidateAnimationReference(job,avatar,eAssetType::Avatar))return job.Fail("Invalid Humanoid source Avatar");
                } else for(const auto& path:descriptor["Tracks"])if(!path.isString()||(!paths.empty()&&!paths.count(path.asString())))return job.Fail("Layer AnimationClip is incompatible with Mask Skeleton");
                if(clip==layer.ReferenceClip) {
                    if(layer.ReferenceTime>descriptor["Duration"].asDouble())return job.Fail("Additive ReferenceTime exceeds clip Duration");
                    const auto tracks=descriptor["Tracks"].size();const auto count=descriptor["FrameCount"].asUInt64();
                    const uint64_t stride=4ull+tracks*40ull;
                    if(!count||count>1000001||!descriptor["BlendShapeTracks"].isArray()||!descriptor["BlendShapeTracks"].empty()
                        ||stride*count>static_cast<uint64_t>(bytes.size()))return job.Fail("Invalid additive reference samples");
                    const auto number=[&](uint64_t offset){quint32 bits=qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData()+offset));float value;std::memcpy(&value,&bits,4);return value;};
                    uint64_t left=0;while(left+1<count&&number((left+1)*stride)<layer.ReferenceTime)++left;
                    const auto right=std::min(left+1,count-1);const float start=number(left*stride),end=number(right*stride);
                    const float fraction=end>start?(layer.ReferenceTime-start)/(end-start):0;
                    for(uint64_t track=0;track<tracks;++track)for(unsigned channel=0;channel<3;++channel) {
                        const auto offset=4+track*40+28+channel*4;
                        const float scale=number(left*stride+offset)*(1-fraction)+number(right*stride+offset)*fraction;
                        if(!std::isfinite(scale)||std::fabs(scale)<0.000001f)return job.Fail("Additive reference pose has a zero or invalid scale");
                    }
                }
            }
        }
        return job.Emit(type,"main",name,AnimationDefinition::ControllerJson(controller,true));
    }
    if(type==eAssetType::AvatarMask) {
        GameSproutRuntime::AvatarMaskData mask;GameSproutRuntime::SkeletonData skeleton;std::string error;
        if(!AvatarMaskDefinition::Read(source,false,mask,error))return job.Fail(error);
        if(mask.Mode==GameSproutRuntime::AvatarMaskMode::Transform) {if(!ValidateMaskSkeleton(job,mask,skeleton))return false;}
        else if(!AvatarMaskDefinition::Validate(mask,skeleton,error))return job.Fail(error);
        return job.Emit(type,"main",name,AvatarMaskDefinition::Write(mask,true));
    }
    if(type!=eAssetType::AnimationClip)return job.Fail("Animation importer received an unsupported resource type");
    std::string error;
    if(!AnimationDefinition::Keys(source,{"Version","Duration","SampleRate","Tracks","Frames","Parents"},error))return job.Fail(error);
    if(source["Version"]!=1||!source["Duration"].isNumeric()||!source["SampleRate"].isNumeric()
        ||!source["Tracks"].isArray()||source["Tracks"].empty()||source["Tracks"].size()>100000
        ||!source["Frames"].isArray()||source["Frames"].empty()||source["Frames"].size()>1000001)return job.Fail("Invalid .anim sample document");
    const double duration=source["Duration"].asDouble(),rate=source["SampleRate"].asDouble();
    if(!std::isfinite(duration)||duration<0||duration>1000000||!std::isfinite(rate)||rate<=0||rate>1000)return job.Fail("Invalid animation duration or sample rate");
    std::set<std::string> tracks;
    for(const auto& track:source["Tracks"])if(!track.isString()||track.asString().empty()||!tracks.insert(track.asString()).second)return job.Fail("Animation track paths must be nonempty and unique");
    const std::uint64_t size=source["Frames"].size()*(4ull+40ull*source["Tracks"].size());
    if(size>512ull*1024*1024)return job.Fail("Animation samples exceed 512 MiB limit");
    QByteArray payload;QDataStream stream(&payload,QIODevice::WriteOnly);stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    double previous=-1;
    for(const auto& frame:source["Frames"]){
        if(job.Cancelled())return false;
        if(!AnimationDefinition::Keys(frame,{"Time","Poses"},error))return job.Fail(error);
        double time;
        if(!AnimationDefinition::Number(frame["Time"],time)||time<0||time>duration||time<=previous||(previous<0&&time!=0)
            ||!frame["Poses"].isArray()||frame["Poses"].size()!=source["Tracks"].size())return job.Fail("Invalid animation sample time or pose count");
        previous=time;stream<<static_cast<float>(time);
        for(const auto& pose:frame["Poses"]){
            if(!AnimationDefinition::Keys(pose,{"Translation","Rotation","Scale"},error))return job.Fail(error);
            if(!FloatArray(pose["Translation"],stream,3)||!FloatArray(pose["Rotation"],stream,4)||!FloatArray(pose["Scale"],stream,3))return job.Fail("Invalid animation TRS sample");
            const auto& rotation=pose["Rotation"];double length=0;for(const auto& value:rotation)length+=value.asDouble()*value.asDouble();
            if(length<1e-12||!std::isfinite(length))return job.Fail("Invalid zero quaternion in animation");
        }
    }
    if(std::fabs(previous-duration)>0.00001)return job.Fail("Last animation sample must equal Duration");
    Json::Value data;data["Duration"]=duration;data["SampleRate"]=rate;data["FrameCount"]=source["Frames"].size();data["Tracks"]=source["Tracks"];
    data["BlendShapeTracks"]=Json::Value(Json::arrayValue);data["Layout"]="float32:Time;per-track:Translation3,RotationQuaternion4,Scale3;per-blend-track:Weight1";
    std::vector<std::int32_t> sourceAvatarParents;
    if(job.Settings.isMember("SourceAvatar")) {
        GameSproutRuntime::ResourceReference reference;Json::Value descriptor,body;QByteArray bytes;GameSproutRuntime::HumanoidAvatarData avatar;
        if(!AnimationDefinition::Reference(job.Settings["SourceAvatar"],reference)||!ReadAnimationPrepared(job,reference,eAssetType::Avatar,descriptor,bytes)
            ||descriptor["Encoding"]!="BinaryValueLE1"||!AssetValueBinary::Decode(bytes,body)||!HumanoidDefinition::ReadAvatar(body,avatar,error))return job.Fail(error.empty()?"Invalid native animation SourceAvatar":error);
        Json::Value skeletonDescriptor,skeletonBody;QByteArray skeletonBytes;
        if(!ReadAnimationPrepared(job,avatar.Skeleton,eAssetType::Skeleton,skeletonDescriptor,skeletonBytes)
            ||skeletonDescriptor["Encoding"]!="BinaryValueLE1"||!AssetValueBinary::Decode(skeletonBytes,skeletonBody)||!skeletonBody["Nodes"].isArray())return job.Fail("SourceAvatar Skeleton is unavailable");
        GameSproutRuntime::SkeletonData hierarchy;std::map<std::string,std::size_t> skeletonPaths,trackIndices;
        for(const auto& value:skeletonBody["Nodes"]) {
            GameSproutRuntime::SkeletonNode node;
            if(!value["Path"].isString()||!value["Name"].isString()||!value["Parent"].isInt()||!HumanoidDefinition::Pose(value["Transform"],node.Transform))return job.Fail("Invalid SourceAvatar Skeleton node");
            node.Path=value["Path"].asString();node.Name=value["Name"].asString();node.Parent=value["Parent"].asInt();
            if(!skeletonPaths.emplace(node.Path,hierarchy.Nodes.size()).second)return job.Fail("Duplicate SourceAvatar Skeleton path");
            hierarchy.Nodes.push_back(node);
        }
        if(!GameSproutRuntime::ValidateHumanAvatar(hierarchy,avatar,error))return job.Fail(error);
        for(Json::ArrayIndex index=0;index<source["Tracks"].size();++index)trackIndices.emplace(source["Tracks"][index].asString(),index);
        for(const auto& track:source["Tracks"]) {
            const auto found=skeletonPaths.find(track.asString());if(found==skeletonPaths.end())return job.Fail("Native Humanoid animation track does not belong to SourceAvatar Skeleton");
            const auto parent=hierarchy.Nodes[found->second].Parent;
            if(parent<0)sourceAvatarParents.push_back(-1);
            else {const auto entry=trackIndices.find(hierarchy.Nodes[parent].Path);sourceAvatarParents.push_back(entry==trackIndices.end()?-2:static_cast<std::int32_t>(entry->second));}
        }
        data["SourceAvatar"]=job.Settings["SourceAvatar"];
    }
    AnimationClipImportOptions options;
    if(job.Settings.isMember("ClipOptions")&&!AnimationImportSettingsJson::Read(job.Settings["ClipOptions"],options,false))return job.Fail("Invalid native animation ClipOptions");
    if(!sourceAvatarParents.empty()&&options.ExtractRootMotion&&!source.isMember("Parents"))return job.Fail("Humanoid root motion requires explicit complete Parents matching SourceAvatar Skeleton");
    std::vector<std::int32_t> parents;
    if(source.isMember("Parents")) {
        if(!source["Parents"].isArray()||source["Parents"].size()!=source["Tracks"].size())return job.Fail("Animation Parents must match Tracks");
        for(const auto& parent:source["Parents"]) {if(!parent.isInt())return job.Fail("Animation Parents requires integer indices");parents.push_back(parent.asInt());}
    } else if(source["Tracks"].size()==1)parents.push_back(-1);
    if(!sourceAvatarParents.empty()&&source.isMember("Parents"))for(std::size_t index=0;index<parents.size();++index)
        if(sourceAvatarParents[index]<-1||parents[index]!=sourceAvatarParents[index])return job.Fail("Native animation Parents contradict SourceAvatar Skeleton; root-motion ancestry must be complete");
    if(!PrepareAnimationClip(job,data,payload,options,parents))return false;
    return job.Emit(type,"main",name,data,payload);
}
