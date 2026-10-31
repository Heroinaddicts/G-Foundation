#include "HumanRetarget.h"
#include "AnimationSampling.h"
#include "AnimationMotion.h"
#include <set>
namespace GameSproutRuntime::HumanRetarget {
bool Sample(const AnimationClipData& clip,const SkeletonData& source,const HumanoidAvatarData& sourceAvatar,
    const SkeletonData& target,const HumanoidAvatarData& targetAvatar,double time,bool loop,bool strip,
    std::vector<TransformPose>& output,std::vector<float>& coverage,std::string& error){
    std::vector<TransformPose> sampled,sourceWorld,sourceReference,targetReference;
    if(!AnimationSampling::Sample(clip,source,time,loop,sampled,error))return false;
    const std::set<std::string> authored(clip.Tracks.begin(),clip.Tracks.end());
    for(std::size_t i=0;i<source.Nodes.size();++i)if(!authored.count(source.Nodes[i].Path))sampled[i]=sourceAvatar.ReferencePose[i];
    if(strip&&!AnimationMotion::Strip(clip,source,time,loop,sampled,error))return false;
    if(!HumanMath::World(source,sampled,sourceWorld,error)||!HumanMath::World(source,sourceAvatar.ReferencePose,sourceReference,error)
        ||!HumanMath::World(target,targetAvatar.ReferencePose,targetReference,error))return false;
    const auto from=HumanMath::Indices(source,sourceAvatar),to=HumanMath::Indices(target,targetAvatar);
    std::array<Quaternion,static_cast<unsigned>(HumanBone::Count)> sourceAxes,targetAxes;
    for(const auto& mapping:sourceAvatar.Bones)sourceAxes[unsigned(mapping.Bone)]=mapping.Axis;
    for(const auto& mapping:targetAvatar.Bones)targetAxes[unsigned(mapping.Bone)]=mapping.Axis;
    std::set<std::string> written(clip.Tracks.begin(),clip.Tracks.end());
    std::map<int,unsigned> targetRoles;for(unsigned role=0;role<to.size();++role)if(to[role]>=0&&from[role]>=0)targetRoles[to[role]]=role;
    output=targetAvatar.ReferencePose;coverage.assign(output.size(),1);
    std::vector<TransformPose> world(output.size());std::vector<bool> done(output.size(),false);
    for(std::size_t start=0;start<output.size();++start){std::vector<int> chain;int cursor=int(start);
        while(cursor>=0&&!done[cursor]){chain.push_back(cursor);cursor=target.Nodes[cursor].Parent;}
        while(!chain.empty()){const int index=chain.back();chain.pop_back();const int parent=target.Nodes[index].Parent;
            auto desired=output[index];const auto role=targetRoles.find(index);
            if(role!=targetRoles.end()){
                const unsigned human=role->second;const int sourceIndex=from[human];
                const auto delta=HumanMath::Product(sourceWorld[sourceIndex].Rotation,HumanMath::Inverse(sourceReference[sourceIndex].Rotation));
                const auto frame=HumanMath::Product(targetAxes[human],HumanMath::Inverse(sourceAxes[human]));
                const auto targetWorld=HumanMath::Product(HumanMath::Product(HumanMath::Product(frame,delta),HumanMath::Inverse(frame)),targetReference[index].Rotation);
                desired.Rotation=parent>=0?HumanMath::Product(HumanMath::Inverse(world[parent].Rotation),targetWorld):targetWorld;
                if(human==unsigned(HumanBone::Hips)){
                    const auto displacement=HumanMath::Rotate(frame,sourceWorld[sourceIndex].Position-sourceReference[sourceIndex].Position)*(targetAvatar.HumanScale/sourceAvatar.HumanScale);
                    const auto position=targetReference[index].Position+displacement;
                    desired.Position=parent>=0?HumanMath::Multiply(HumanMath::Rotate(HumanMath::Inverse(world[parent].Rotation),position-world[parent].Position),{1/world[parent].Scale.X,1/world[parent].Scale.Y,1/world[parent].Scale.Z}):position;
                }
                int at=sourceIndex;while(at>=0){if(written.count(source.Nodes[at].Path)){coverage[index]=1;break;}at=source.Nodes[at].Parent;}
            }
            output[index]=desired;world[index]=desired;
            if(parent>=0){world[index].Position=world[parent].Position+HumanMath::Rotate(world[parent].Rotation,HumanMath::Multiply(world[parent].Scale,desired.Position));world[index].Rotation=HumanMath::Product(world[parent].Rotation,desired.Rotation);world[index].Scale=HumanMath::Multiply(world[parent].Scale,desired.Scale);}
            done[index]=true;
        }
    }return true;
}
}
