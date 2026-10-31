#include "LibraryRuntimeResources.h"
#include "LibraryResourceDecode.h"
#include "../Shared/AnimationImportSettingsJson.h"
#include "../Shared/HumanoidDefinition.h"
#include <set>

namespace {
bool Nodes(const Json::Value& source,std::vector<GameSproutRuntime::SkeletonNode>& output) {
    if(!source.isArray()||source.empty()||source.size()>100000)return false;
    std::set<std::string> paths;
    for(const auto& value:source) {
        GameSproutRuntime::SkeletonNode node;
        if(!value.isObject()||!value["Path"].isString()||value["Path"].asString().empty()
            ||!paths.insert(value["Path"].asString()).second||!value["Name"].isString()
            ||!value["Parent"].isInt()||value["Parent"].asInt()<-1
            ||value["Parent"].asInt()>=static_cast<int>(source.size())
            ||!LibraryResourceDecode::PoseValue(value["Transform"],node.Transform))return false;
        if(value.isMember("InheritMode") && value["InheritMode"]!=0)return false;
        node.Path=value["Path"].asString();node.Name=value["Name"].asString();node.Parent=value["Parent"].asInt();
        output.push_back(std::move(node));
    }
    // Linear cycle validation; each node has at most one parent.
    std::vector<unsigned char> marks(output.size());
    for(std::size_t index=0;index<output.size();++index) {
        int cursor=static_cast<int>(index);
        while(cursor>=0 && marks[cursor]==0){marks[cursor]=1;cursor=output[cursor].Parent;}
        if(cursor>=0 && marks[cursor]==1)return false;
        cursor=static_cast<int>(index);
        while(cursor>=0 && marks[cursor]==1){marks[cursor]=2;cursor=output[cursor].Parent;}
    }
    return true;
}
bool Body(const Json::Value& descriptor,const QByteArray& bytes,Json::Value& body) {
    return descriptor["Encoding"]=="BinaryValueLE1" && AssetValueBinary::Decode(bytes,body) && body.isObject();
}
}

bool LibraryRuntimeResources::LoadModel(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::ModelData& output,std::string& error) {
    output={};Json::Value descriptor,body;QByteArray bytes;
    if(!Read(reference,eAssetType::Model,descriptor,bytes,error))return false;
    const auto fail=[&error](){error="Invalid prepared Model hierarchy or references";return false;};
    std::vector<GameSproutRuntime::SkeletonNode> nodes;
    if(!Body(descriptor,bytes,body)||body["CoordinateSystem"]!="RightHandedYUp"||body["UnitMeters"]!=1.0
        ||!Nodes(body["Nodes"],nodes)||!body["Animations"].isArray())return fail();
    GameSproutRuntime::ModelData data;
    if(body.isMember("Skeleton")&&!LibraryResourceDecode::ReferenceValue(body["Skeleton"],data.Skeleton))return fail();
    if(body.isMember("Avatar")&&!LibraryResourceDecode::ReferenceValue(body["Avatar"],data.Avatar))return fail();
    for(const auto& value:body["Animations"]) {
        GameSproutRuntime::ResourceReference clip;
        if(!LibraryResourceDecode::ReferenceValue(value,clip))return fail();
        data.Animations.push_back(std::move(clip));
    }
    for(std::size_t index=0;index<nodes.size();++index) {
        const auto& value=body["Nodes"][static_cast<Json::ArrayIndex>(index)];
        GameSproutRuntime::ModelNode node;static_cast<GameSproutRuntime::SkeletonNode&>(node)=std::move(nodes[index]);
        if(!LibraryResourceDecode::PoseValue(value["GeometryTransform"],node.GeometryTransform)
            ||!value["Materials"].isArray()||value["Materials"].size()>1024)return fail();
        if(value.isMember("Mesh")&&!LibraryResourceDecode::ReferenceValue(value["Mesh"],node.Mesh))return fail();
        for(const auto& material:value["Materials"]) {
            GameSproutRuntime::ResourceReference ref;
            if(!LibraryResourceDecode::ReferenceValue(material,ref))return fail();
            node.Materials.push_back(std::move(ref));
        }
        data.Nodes.push_back(std::move(node));
    }
    output=std::move(data);return true;
}

bool LibraryRuntimeResources::LoadSkeleton(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::SkeletonData& output,std::string& error) {
    output={};Json::Value descriptor,body;QByteArray bytes;
    if(!Read(reference,eAssetType::Skeleton,descriptor,bytes,error))return false;
    GameSproutRuntime::SkeletonData data;
    if(!Body(descriptor,bytes,body)||!Nodes(body["Nodes"],data.Nodes)) {
        error="Invalid prepared Skeleton hierarchy";return false;
    }
    output=std::move(data);return true;
}

bool LibraryRuntimeResources::LoadAnimationClip(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::AnimationClipData& output,std::string& error) {
    output={};Json::Value descriptor;QByteArray bytes;
    if(!Read(reference,eAssetType::AnimationClip,descriptor,bytes,error))return false;
    const auto fail=[&error](){error="Invalid prepared AnimationClip samples or track bindings";return false;};
    std::uint32_t frames=0;
    if(descriptor["Format"]!="AnimationClip2"||descriptor["Encoding"]!="RawBytes"||!descriptor["Duration"].isNumeric()||!descriptor["SampleRate"].isNumeric()
        ||!LibraryResourceDecode::Unsigned(descriptor["FrameCount"],frames)||frames==0||frames>1000001
        ||!descriptor["Tracks"].isArray()||descriptor["Tracks"].empty()||descriptor["Tracks"].size()>100000
        ||!descriptor["BlendShapeTracks"].isArray())return fail();
    GameSproutRuntime::AnimationClipData data;
    data.Duration=descriptor["Duration"].asFloat();data.SampleRate=descriptor["SampleRate"].asFloat();
    if(descriptor.isMember("SourceAvatar")&&!LibraryResourceDecode::ReferenceValue(descriptor["SourceAvatar"],data.SourceAvatar))return fail();
    if(!std::isfinite(data.Duration)||data.Duration<0||!std::isfinite(data.SampleRate)||data.SampleRate<=0||data.SampleRate>1000)return fail();
    std::set<std::string> paths;
    for(const auto& track:descriptor["Tracks"]) {
        if(!track.isString()||track.asString().empty()||!paths.insert(track.asString()).second)return fail();
        data.Tracks.push_back(track.asString());
    }
    const std::uint64_t stride=4ull+data.Tracks.size()*40ull+descriptor["BlendShapeTracks"].size()*4ull;
    const auto& motion=descriptor["RootMotion"];
    std::uint32_t motionCount=0;
    if(!motion.isObject()||!motion["NodePath"].isString()||!motion["Offset"].isUInt64()
        ||!LibraryResourceDecode::Unsigned(motion["Count"],motionCount)||(motionCount&&motionCount!=frames)
        ||motion["Offset"].asUInt64()!=stride*frames
        ||stride*frames+motionCount*28ull!=static_cast<std::uint64_t>(bytes.size()))return fail();
    data.RootMotionNodePath=motion["NodePath"].asString();
    if((!motionCount&&!data.RootMotionNodePath.empty())||(motionCount&&!paths.count(data.RootMotionNodePath)))return fail();
    if(!descriptor["Events"].isArray())return fail();
    AnimationClipImportOptions options;Json::Value settings(Json::objectValue);settings["Events"]=descriptor["Events"];
    if(!AnimationImportSettingsJson::Read(settings,options,false))return fail();
    for(const auto& event:options.Events) {
        if(event.Time>data.Duration)return fail();
        data.Events.push_back({event.Time,event.Name,event.Payload});
    }
    // Skeletal TRS is supported now; morph curves require the separate deformation path.
    if(!descriptor["BlendShapeTracks"].empty()){error="AnimationClip contains unsupported BlendShape tracks";return false;}
    data.Frames.reserve(frames);
    for(std::uint32_t index=0;index<frames;++index) {
        GameSproutRuntime::AnimationFrame frame;
        std::uint64_t offset=stride*index;
        frame.Time=LibraryResourceDecode::FloatAt(bytes,offset);offset+=4;
        if(!std::isfinite(frame.Time)||frame.Time<0||frame.Time>data.Duration+0.0001f
            ||(index==0&&frame.Time!=0)||(index&&frame.Time<=data.Frames.back().Time))return fail();
        frame.Poses.reserve(data.Tracks.size());
        for(std::size_t track=0;track<data.Tracks.size();++track) {
            float values[10];
            for(unsigned channel=0;channel<10;++channel){values[channel]=LibraryResourceDecode::FloatAt(bytes,offset);offset+=4;if(!std::isfinite(values[channel]))return fail();}
            GameSproutRuntime::TransformPose pose;
            pose.Position={values[0],values[1],values[2]};pose.Rotation={values[3],values[4],values[5],values[6]};pose.Scale={values[7],values[8],values[9]};
            if(!pose.Rotation.TryNormalize(pose.Rotation))return fail();
            frame.Poses.push_back(pose);
        }
        data.Frames.push_back(std::move(frame));
    }
    if(std::fabs(data.Frames.back().Time-data.Duration)>0.0001f)return fail();
    const auto motionOffset=motion["Offset"].asUInt64();
    for(std::uint32_t index=0;index<motionCount;++index) {
        const auto offset=motionOffset+index*28ull;float values[7];
        for(unsigned channel=0;channel<7;++channel) {
            values[channel]=LibraryResourceDecode::FloatAt(bytes,offset+channel*4);
            if(!std::isfinite(values[channel]))return fail();
        }
        GameSproutRuntime::RootMotionDelta delta;
        delta.Translation={values[0],values[1],values[2]};delta.Rotation={values[3],values[4],values[5],values[6]};
        if(!delta.Rotation.TryNormalize(delta.Rotation))return fail();
        if(index==0&&(delta.Translation.X!=0||delta.Translation.Y!=0||delta.Translation.Z!=0||
            std::fabs(delta.Rotation.X)>0.00001f||std::fabs(delta.Rotation.Y)>0.00001f||std::fabs(delta.Rotation.Z)>0.00001f||std::fabs(std::fabs(delta.Rotation.W)-1)>0.00001f))return fail();
        data.RootMotion.push_back(delta);
    }
    output=std::move(data);return true;
}

// Avatar profile is prepared CPU data. Its source rig and editor objects never leak into Runtime.
bool LibraryRuntimeResources::LoadHumanoidAvatar(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::HumanoidAvatarData& output,std::string& error) {
    output={};Json::Value descriptor,body;QByteArray bytes;GameSproutRuntime::HumanoidAvatarData avatar;GameSproutRuntime::SkeletonData skeleton;
    if(!Read(reference,eAssetType::Avatar,descriptor,bytes,error)||!Body(descriptor,bytes,body)
        ||!HumanoidDefinition::ReadAvatar(body,avatar,error)||!LoadSkeleton(avatar.Skeleton,skeleton,error)
        ||!GameSproutRuntime::ValidateHumanAvatar(skeleton,avatar,error)) {if(error.empty())error="Invalid prepared Humanoid Avatar";return false;}
    output=std::move(avatar);return true;
}
