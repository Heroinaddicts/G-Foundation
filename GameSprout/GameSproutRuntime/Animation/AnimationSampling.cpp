#include "AnimationSampling.h"
#include "RuntimeMath.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <map>

namespace GameSproutRuntime::AnimationSampling {
namespace {
bool Fail(std::string& error,const char* message){error=message;return false;}
bool ValidPose(const TransformPose& p) {Quaternion q;return RuntimeMath::IsFinite(p.Position)&&RuntimeMath::IsFinite(p.Scale)&&p.Rotation.TryNormalize(q);}
}
bool Validate(const SkeletonData& data,std::string& error) {
    if(data.Nodes.empty()||data.Nodes.size()>100000)return Fail(error,"Skeleton node count is invalid");
    std::set<std::string> paths;
    for(std::size_t i=0;i<data.Nodes.size();++i){const auto& n=data.Nodes[i];
        if(n.Path.empty()||!paths.insert(n.Path).second||!ValidPose(n.Transform)||n.Parent < -1 || n.Parent>=std::int64_t(data.Nodes.size()))return Fail(error,"Invalid skeleton node, path or transform");
    }
    // Linear, iterative cycle check; deep imported skeletons never recurse or rescan ancestors.
    std::vector<std::uint8_t> colors(data.Nodes.size(),0);
    for(std::size_t i=0;i<data.Nodes.size();++i){
        auto node=std::int64_t(i);
        while(node>=0&&!colors[node]){colors[node]=1;node=data.Nodes[node].Parent;}
        if(node>=0&&colors[node]==1)return Fail(error,"Skeleton contains a parent cycle");
        node=std::int64_t(i);while(node>=0&&colors[node]==1){colors[node]=2;node=data.Nodes[node].Parent;}
    }
    return true;
}
bool Validate(const AnimationClipData& data,std::string& error) {
    if(!std::isfinite(data.Duration)||data.Duration<0||!std::isfinite(data.SampleRate)||data.SampleRate<=0||data.Tracks.empty()||data.Tracks.size()>100000||data.Frames.empty()||data.Frames.size()>1000001)return Fail(error,"AnimationClip dimensions are invalid");
    std::set<std::string> paths;for(const auto& path:data.Tracks)if(path.empty()||!paths.insert(path).second)return Fail(error,"AnimationClip has duplicate or empty track paths");
    float previous=-1;
    for(const auto& frame:data.Frames){if(!std::isfinite(frame.Time)||frame.Time<0||frame.Time>data.Duration+1e-4f||frame.Time<=previous||frame.Poses.size()!=data.Tracks.size())return Fail(error,"AnimationClip frame layout or time is invalid");
        for(const auto& pose:frame.Poses)if(!ValidPose(pose))return Fail(error,"AnimationClip contains an invalid pose");previous=frame.Time;
    }
    if(data.Frames.front().Time!=0||std::fabs(data.Frames.back().Time-data.Duration)>1e-4f)return Fail(error,"AnimationClip samples must include both time endpoints");
    if(!data.RootMotion.empty()) {
        if(data.RootMotion.size()!=data.Frames.size()||!paths.count(data.RootMotionNodePath))return Fail(error,"Root-motion samples require a bound source path and matching frame count");
        for(const auto& motion:data.RootMotion){Quaternion q;if(!RuntimeMath::IsFinite(motion.Translation)||!motion.Rotation.TryNormalize(q))return Fail(error,"Invalid root-motion rigid transform");}
        const auto& first=data.RootMotion.front();
        if(std::fabs(first.Translation.X)>1e-4f||std::fabs(first.Translation.Y)>1e-4f||std::fabs(first.Translation.Z)>1e-4f||std::fabs(first.Rotation.X)>1e-4f||std::fabs(first.Rotation.Y)>1e-4f||std::fabs(first.Rotation.Z)>1e-4f||std::fabs(std::fabs(first.Rotation.W)-1)>1e-4f)return Fail(error,"Root motion must start at identity");
    }else if(!data.RootMotionNodePath.empty())return Fail(error,"Root-motion path without samples");
    if(data.Events.size()>4096)return Fail(error,"AnimationClip has too many events");
    float lastEvent=-1;
    for(const auto& event:data.Events){if(!std::isfinite(event.Time)||event.Time<0||event.Time>data.Duration||event.Time<lastEvent||event.Name.empty()||event.Name.size()>256||event.Name.find('\0')!=std::string::npos||event.Payload.size()>65536)return Fail(error,"Invalid or unsorted animation event");lastEvent=event.Time;}
    return true;
}
bool Validate(const AnimatorControllerData& input,std::string& error) {
    AnimatorControllerData data=input;
    for(std::size_t index=0;index<input.Layers.size();++index){const auto& layer=input.Layers[index];
        if(layer.SyncedLayerIndex< -1||layer.SyncedLayerIndex>=int(input.Layers.size())||layer.SyncedLayerIndex==int(index))return Fail(error,"Invalid or cyclic synchronized layer");
        if(layer.SyncedLayerIndex<0){if(!layer.MotionOverrides.empty()||layer.SyncedLayerAffectsTiming)return Fail(error,"Only synchronized layers accept motion overrides or timing");continue;}
        if(index==0||input.Layers[layer.SyncedLayerIndex].SyncedLayerIndex>=0||!layer.States.empty()||!layer.Transitions.empty()||layer.DefaultState!=0)return Fail(error,"Synchronized layer must reference an independent state machine and cannot own topology");
        if(layer.SyncedLayerAffectsTiming&&layer.BlendingMode!=AnimatorLayerBlendingMode::Override)return Fail(error,"Additive sync layer cannot affect timing");
        auto& resolved=data.Layers[index];const auto& source=input.Layers[layer.SyncedLayerIndex];resolved.States=source.States;resolved.Transitions=source.Transitions;resolved.DefaultState=source.DefaultState;
        std::set<std::string> overridden;
        for(const auto& motion:layer.MotionOverrides){if(!overridden.insert(motion.State).second)return Fail(error,"Duplicate synchronized motion override");
            bool found=false;for(auto& state:resolved.States)if(state.Name==motion.State){found=true;state.Clip=motion.Clip;state.BlendParameter=motion.BlendParameter;state.Motions=motion.Motions;}
            if(!found)return Fail(error,"Synchronized override points to missing source state");}
    }

    if(data.Layers.empty()||data.Layers.size()>64)return Fail(error,"Controller must contain 1..64 layers");
    std::map<std::string,AnimatorParameterType> parameters;
    for(const auto& p:data.Parameters)if(p.Name.empty()||!parameters.emplace(p.Name,p.Type).second||!std::isfinite(p.DefaultValue)||p.Type>AnimatorParameterType::Trigger || ((p.Type==AnimatorParameterType::Bool||p.Type==AnimatorParameterType::Trigger)&&p.DefaultValue!=0&&p.DefaultValue!=1) || (p.Type==AnimatorParameterType::Int&&std::trunc(p.DefaultValue)!=p.DefaultValue))return Fail(error,"Controller parameter declaration is invalid");
    std::set<std::string> layerNames;
    for(std::size_t index=0;index<data.Layers.size();++index){const auto& layer=data.Layers[index];
        if(layer.Name.empty()||!layerNames.insert(layer.Name).second||layer.States.empty()||layer.States.size()>65536||layer.DefaultState>=layer.States.size()||layer.BlendingMode>AnimatorLayerBlendingMode::Additive||!std::isfinite(layer.DefaultWeight)||layer.DefaultWeight<0||layer.DefaultWeight>1||!std::isfinite(layer.ReferenceTime)||layer.ReferenceTime<0)return Fail(error,"Controller layer declaration is invalid");
        if(index==0&&(layer.BlendingMode!=AnimatorLayerBlendingMode::Override||layer.DefaultWeight!=1||!layer.Mask.Guid.empty()||layer.Mask.LocalID||!layer.ReferenceClip.Guid.empty()||layer.ReferenceClip.LocalID||layer.ReferenceTime!=0))return Fail(error,"Base layer must be unmasked Override with weight 1 and no additive reference");
        if((layer.Mask.Guid.empty())!=(layer.Mask.LocalID==0)||(layer.ReferenceClip.Guid.empty())!=(layer.ReferenceClip.LocalID==0))return Fail(error,"Layer contains an incomplete resource reference");
        if(layer.BlendingMode!=AnimatorLayerBlendingMode::Additive&&(!layer.ReferenceClip.Guid.empty()||layer.ReferenceTime!=0))return Fail(error,"Only Additive layers accept reference poses");
        if(layer.ReferenceClip.Guid.empty()&&layer.ReferenceTime!=0)return Fail(error,"ReferenceTime requires a reference clip");
    std::set<std::string> names;
    for(const auto& s:layer.States){if(s.Name.empty()||!names.insert(s.Name).second||!std::isfinite(s.Speed)||s.Speed<0)return Fail(error,"Controller state is invalid");
        if(s.BlendParameter.empty()){if(s.Clip.Guid.empty()||!s.Clip.LocalID||!s.Motions.empty())return Fail(error,"Clip state must reference one prepared clip");}
        else {const auto parameter=parameters.find(s.BlendParameter);if(parameter==parameters.end()||parameter->second!=AnimatorParameterType::Float||s.Motions.empty())return Fail(error,"1D blend state requires a Float parameter and motions");
            float previous=-INFINITY;for(const auto& motion:s.Motions){if(!std::isfinite(motion.Threshold)||motion.Threshold<=previous||!std::isfinite(motion.Speed)||motion.Speed<=0||motion.Clip.Guid.empty()||!motion.Clip.LocalID)return Fail(error,"1D blend thresholds or clips are invalid");previous=motion.Threshold;}}
    }
    for(const auto& t:layer.Transitions){if(t.From < -1||t.From>=std::int64_t(layer.States.size())||t.To>=layer.States.size()||!std::isfinite(t.Duration)||t.Duration<0||!std::isfinite(t.ExitTime)||t.ExitTime<0||!std::isfinite(t.TargetOffset)||t.TargetOffset<0||(!t.HasExitTime&&t.Conditions.empty()))return Fail(error,"Controller transition declaration is invalid");
        for(const auto& c:t.Conditions)if(!parameters.count(c.Parameter)||c.Compare>AnimatorCompare::IfNot||!std::isfinite(c.Value))return Fail(error,"Controller condition declaration is invalid");
    }
    }return true;
}
TransformPose Blend(const TransformPose& a,const TransformPose& b,float weight) {
    const float w=std::clamp(weight,0.f,1.f);TransformPose result;
    result.Position=a.Position*(1-w)+b.Position*w;result.Scale=a.Scale*(1-w)+b.Scale*w;
    Quaternion qa,qb;a.Rotation.TryNormalize(qa);b.Rotation.TryNormalize(qb);
    float dot=qa.X*qb.X+qa.Y*qb.Y+qa.Z*qb.Z+qa.W*qb.W;if(dot<0){qb={-qb.X,-qb.Y,-qb.Z,-qb.W};dot=-dot;}
    float left=1-w,right=w;
    if(dot<0.9995f){const float angle=std::acos(std::clamp(dot,-1.f,1.f)),inv=1/std::sin(angle);left=std::sin((1-w)*angle)*inv;right=std::sin(w*angle)*inv;}
    Quaternion q{qa.X*left+qb.X*right,qa.Y*left+qb.Y*right,qa.Z*left+qb.Z*right,qa.W*left+qb.W*right};q.TryNormalize(result.Rotation);return result;
}
void BlendContributions(std::vector<TransformPose>& poses,std::vector<float>& coverage,
    const std::vector<TransformPose>& other,const std::vector<float>& otherCoverage,float weight){
    const float blend=std::clamp(weight,0.f,1.f);
    for(std::size_t i=0;i<poses.size();++i){
        const float left=coverage[i]*(1-blend),right=otherCoverage[i]*blend,total=left+right;
        if(total>0)poses[i]=Blend(poses[i],other[i],right/total);
        coverage[i]=total;
    }
}
bool Sample(const AnimationClipData& clip,const SkeletonData& skeleton,double seconds,bool loop,std::vector<TransformPose>& output,std::string& error){
    if(!std::isfinite(seconds)||clip.Frames.empty())return Fail(error,"Invalid animation sample time");
    double time=std::max(0.0,seconds);if(loop&&clip.Duration>0)time=std::fmod(time,clip.Duration);else time=std::min(time,double(clip.Duration));
    auto upper=std::upper_bound(clip.Frames.begin(),clip.Frames.end(),time,[](double value,const AnimationFrame& frame){return value<frame.Time;});
    const AnimationFrame& b=upper==clip.Frames.end()?clip.Frames.back():*upper;
    const AnimationFrame& a=upper==clip.Frames.begin()?b:*(upper-1);
    const float factor=b.Time>a.Time?float((time-a.Time)/(b.Time-a.Time)):0;
    std::map<std::string,std::size_t> tracks;for(std::size_t i=0;i<clip.Tracks.size();++i)tracks.emplace(clip.Tracks[i],i);
    // Every source track must resolve; silently animating an unrelated skeleton is an error.
    std::set<std::string> nodes;for(const auto& node:skeleton.Nodes)nodes.insert(node.Path);
    for(const auto& path:clip.Tracks)if(!nodes.count(path))return Fail(error,"AnimationClip track does not belong to the bound skeleton");
    output.resize(skeleton.Nodes.size());for(std::size_t i=0;i<skeleton.Nodes.size();++i){const auto found=tracks.find(skeleton.Nodes[i].Path);output[i]=found==tracks.end()?skeleton.Nodes[i].Transform:Blend(a.Poses[found->second],b.Poses[found->second],factor);}return true;
}
}
