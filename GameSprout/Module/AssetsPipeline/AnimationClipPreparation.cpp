#include "AnimationClipPreparation.h"
#include "../Shared/AnimationImportSettingsJson.h"
#include "Math/TransformMath.h"
#include <QDataStream>
#include <QtEndian>
#include <cstring>
#include <algorithm>

namespace {
float Read(const QByteArray& bytes,std::uint64_t offset) {
    const std::uint32_t bits=qFromLittleEndian<std::uint32_t>(reinterpret_cast<const uchar*>(bytes.constData()+offset));
    float value;std::memcpy(&value,&bits,sizeof(value));return value;
}
bool Hierarchy(const std::vector<std::int32_t>& parents,std::size_t count) {
    if(parents.size()!=count)return false;
    for(auto parent:parents)if(parent< -1||parent>=static_cast<std::int64_t>(count))return false;
    std::vector<unsigned char> marks(count);
    for(std::size_t index=0;index<count;++index) {
        std::int32_t cursor=static_cast<std::int32_t>(index);
        while(cursor>=0&&!marks[cursor]){marks[cursor]=1;cursor=parents[cursor];}
        if(cursor>=0&&marks[cursor]==1)return false;
        cursor=static_cast<std::int32_t>(index);
        while(cursor>=0&&marks[cursor]==1){marks[cursor]=2;cursor=parents[cursor];}
    }
    return true;
}
}

bool PrepareAnimationClip(AssetImportJob& job,Json::Value& descriptor,QByteArray& payload,
    const AnimationClipImportOptions& options,const std::vector<std::int32_t>& parents) {
    if(!AnimationImportSettingsJson::Validate(options,!options.Name.empty()))return job.Fail("Invalid animation ClipOptions");
    const float duration=descriptor["Duration"].asFloat();
    descriptor["Format"]="AnimationClip2";
    descriptor["Events"]=Json::Value(Json::arrayValue);
    for(const auto& event:options.Events) {
        if(event.Time>duration)return job.Fail("Animation event '"+event.Name+"' is outside the clip duration");
        Json::Value value(Json::objectValue);value["Time"]=event.Time;value["Name"]=event.Name;value["Payload"]=event.Payload;
        descriptor["Events"].append(std::move(value));
    }
    const auto tracks=descriptor["Tracks"].size();
    const auto frames=descriptor["FrameCount"].asUInt();
    const std::uint64_t stride=4ull+tracks*40ull+descriptor["BlendShapeTracks"].size()*4ull;
    const std::uint64_t size=stride*frames;
    if(size!=static_cast<std::uint64_t>(payload.size()))return job.Fail("Animation raw sample size mismatch");
    auto& motion=descriptor["RootMotion"];motion=Json::Value(Json::objectValue);
    motion["NodePath"]="";motion["Offset"]=Json::UInt64(size);motion["Count"]=0;
    if(!parents.empty()&&!Hierarchy(parents,tracks))return job.Fail("Animation Parents array must be acyclic and match Tracks");
    if(!options.ExtractRootMotion)return true;
    if(!Hierarchy(parents,tracks))return job.Fail("Root motion requires a complete acyclic Parents array matching Tracks");
    std::int32_t selected=-1;
    for(Json::ArrayIndex index=0;index<tracks;++index)if(descriptor["Tracks"][index].asString()==options.RootMotionNode)selected=static_cast<std::int32_t>(index);
    if(selected<0)return job.Fail("RootMotionNode does not identify an animation track: "+options.RootMotionNode);
    if(size+frames*28ull>512ull*1024*1024)return job.Fail("Animation samples and root motion exceed 512 MiB");
    std::vector<std::int32_t> chain;
    for(auto index=selected;index>=0;index=parents[index])chain.push_back(index);
    std::reverse(chain.begin(),chain.end());
    QByteArray trajectory;
    QDataStream output(&trajectory,QIODevice::WriteOnly);output.setByteOrder(QDataStream::LittleEndian);output.setFloatingPointPrecision(QDataStream::SinglePrecision);
    GameSproutRuntime::Mat4 inverseStart;
    for(std::uint32_t frame=0;frame<frames;++frame) {
        if(job.Cancelled())return false;
        GameSproutRuntime::Mat4 world;
        try {
            for(const auto track:chain) {
                const auto offset=stride*frame+4+std::uint64_t(track)*40;
                const GameSproutRuntime::Vector3 position{Read(payload,offset),Read(payload,offset+4),Read(payload,offset+8)};
                const GameSproutRuntime::Quaternion rotation{Read(payload,offset+12),Read(payload,offset+16),Read(payload,offset+20),Read(payload,offset+24)};
                const GameSproutRuntime::Vector3 scale{Read(payload,offset+28),Read(payload,offset+32),Read(payload,offset+36)};
                world=world*GameSproutRuntime::RuntimeMath::ComposeTRS(position,rotation,scale);
            }
        }catch(const std::exception& exception){return job.Fail(std::string("Invalid root motion transform: ")+exception.what());}
        GameSproutRuntime::Vector3 position,scale;GameSproutRuntime::Quaternion rotation;
        if(!GameSproutRuntime::RuntimeMath::TryDecomposeTRS(world,position,rotation,scale)||scale.X<=0||scale.Y<=0||scale.Z<=0)
            return job.Fail("RootMotionNode global transform contains shear, reflection or zero scale: "+options.RootMotionNode);
        const auto rigid=GameSproutRuntime::RuntimeMath::ComposeTRS(position,rotation,{1,1,1});
        if(!frame&&!GameSproutRuntime::RuntimeMath::TryInverseAffine(rigid,inverseStart))return job.Fail("Root motion start transform is not invertible");
        const auto relative=frame?rigid*inverseStart:GameSproutRuntime::Mat4{};
        if(!GameSproutRuntime::RuntimeMath::TryDecomposeTRS(relative,position,rotation,scale))return job.Fail("Root motion rigid trajectory is invalid");
        output<<position.X<<position.Y<<position.Z<<rotation.X<<rotation.Y<<rotation.Z<<rotation.W;
    }
    payload.append(trajectory);motion["NodePath"]=options.RootMotionNode;motion["Count"]=frames;
    return true;
}
