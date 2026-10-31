#include "AnimationMotion.h"
#include "AnimationSampling.h"
#include "RuntimeMath.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace GameSproutRuntime::AnimationMotion {
namespace {
Quaternion Multiply(Quaternion a,Quaternion b) {
    Quaternion q{a.W*b.X+a.X*b.W+a.Y*b.Z-a.Z*b.Y,a.W*b.Y-a.X*b.Z+a.Y*b.W+a.Z*b.X,
        a.W*b.Z+a.X*b.Y-a.Y*b.X+a.Z*b.W,a.W*b.W-a.X*b.X-a.Y*b.Y-a.Z*b.Z};
    Quaternion unit;q.TryNormalize(unit);return unit;
}
Vector3 Rotate(Quaternion q,Vector3 value) {
    Quaternion unit;q.TryNormalize(unit);
    const auto matrix=RuntimeMath::ComposeTRS({},unit,{1,1,1});
    const auto v=matrix*Vector4(value.X,value.Y,value.Z,0);return {v.X,v.Y,v.Z};
}
bool Finite(const RootMotionDelta& motion){Quaternion q;return RuntimeMath::IsFinite(motion.Translation)&&motion.Rotation.TryNormalize(q);}
RootMotionDelta Wrapped(const AnimationClipData& clip,double time) {
    const auto upper=std::upper_bound(clip.Frames.begin(),clip.Frames.end(),time,[](double value,const AnimationFrame& f){return value<f.Time;});
    const std::size_t right=upper==clip.Frames.end()?clip.Frames.size()-1:std::size_t(upper-clip.Frames.begin());
    const std::size_t left=right?right-1:right;
    const float factor=clip.Frames[right].Time>clip.Frames[left].Time?float((time-clip.Frames[left].Time)/(clip.Frames[right].Time-clip.Frames[left].Time)):0;
    return Blend(clip.RootMotion[left],clip.RootMotion[right],factor);
}
}
RootMotionDelta Compose(const RootMotionDelta& a,const RootMotionDelta& b){return {a.Translation+Rotate(a.Rotation,b.Translation),Multiply(a.Rotation,b.Rotation)};}
RootMotionDelta Inverse(const RootMotionDelta& value){Quaternion unit;value.Rotation.TryNormalize(unit);Quaternion inverse{-unit.X,-unit.Y,-unit.Z,unit.W};return {Rotate(inverse,value.Translation*-1.f),inverse};}
RootMotionDelta Blend(const RootMotionDelta& a,const RootMotionDelta& b,float weight){const auto pose=AnimationSampling::Blend({a.Translation,a.Rotation,{1,1,1}},{b.Translation,b.Rotation,{1,1,1}},weight);return {pose.Position,pose.Rotation};}
Mat4 Matrix(const RootMotionDelta& value){Quaternion unit;value.Rotation.TryNormalize(unit);return RuntimeMath::ComposeTRS(value.Translation,unit,{1,1,1});}
bool Sample(const AnimationClipData& clip,double time,bool loop,RootMotionDelta& output,std::string& error){
    output={};if(clip.RootMotion.empty())return true;
    if(!std::isfinite(time)||time<0||clip.RootMotion.size()!=clip.Frames.size()){error="Invalid root-motion sample time or layout";return false;}
    if(!loop||clip.Duration<=0){output=Wrapped(clip,std::min(time,double(clip.Duration)));return true;}
    const double loops=std::floor(time/clip.Duration);
    if(loops>=double(std::numeric_limits<std::uint64_t>::max())){error="Root-motion loop count exceeds uint64 range";return false;}
    auto count=static_cast<std::uint64_t>(loops);RootMotionDelta power=clip.RootMotion.back();
    while(count){if(count&1)output=Compose(output,power);count>>=1;if(count)power=Compose(power,power);if(!Finite(output)||!Finite(power)){error="Root-motion accumulation exceeds finite transform range";return false;}}
    output=Compose(output,Wrapped(clip,std::fmod(time,clip.Duration)));
    if(!Finite(output)){error="Root-motion sample overflow";return false;}return true;
}
bool Delta(const AnimationClipData& clip,double previous,double current,bool loop,RootMotionDelta& output,std::string& error){
    RootMotionDelta a,b;if(!Sample(clip,previous,loop,a,error)||!Sample(clip,current,loop,b,error))return false;
    output=Compose(Inverse(a),b);if(!Finite(output)){error="Root-motion delta overflow";return false;}return true;
}
bool Strip(const AnimationClipData& clip,const SkeletonData& skeleton,double time,bool loop,std::vector<TransformPose>& pose,std::string& error){
    if(clip.RootMotion.empty())return true;
    const double wrapped=loop&&clip.Duration>0?std::fmod(std::max(time,0.0),clip.Duration):std::clamp(time,0.0,double(clip.Duration));
    const Mat4 inverse=Matrix(Inverse(Wrapped(clip,wrapped)));
    for(std::size_t i=0;i<pose.size();++i)if(skeleton.Nodes[i].Parent<0){
        const Mat4 local=inverse*RuntimeMath::ComposeTRS(pose[i].Position,pose[i].Rotation,pose[i].Scale);
        if(!RuntimeMath::TryDecomposeTRS(local,pose[i].Position,pose[i].Rotation,pose[i].Scale)){error="Root-motion stripping produced a non-TRS root";return false;}
    }return true;
}
}
