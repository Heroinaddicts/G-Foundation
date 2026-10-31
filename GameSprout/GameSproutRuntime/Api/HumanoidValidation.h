#pragma once
#include "RuntimeAnimationData.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>

namespace GameSproutRuntime {
inline const char* HumanBoneName(HumanBone bone) {
    static const char* names[]={"Hips","LeftUpperLeg","RightUpperLeg","LeftLowerLeg","RightLowerLeg","LeftFoot","RightFoot","Spine","Chest","Neck","Head","LeftShoulder","RightShoulder","LeftUpperArm","RightUpperArm","LeftLowerArm","RightLowerArm","LeftHand","RightHand","LeftToes","RightToes","LeftEye","RightEye","Jaw","LeftThumbProximal","LeftThumbIntermediate","LeftThumbDistal","LeftIndexProximal","LeftIndexIntermediate","LeftIndexDistal","LeftMiddleProximal","LeftMiddleIntermediate","LeftMiddleDistal","LeftRingProximal","LeftRingIntermediate","LeftRingDistal","LeftLittleProximal","LeftLittleIntermediate","LeftLittleDistal","RightThumbProximal","RightThumbIntermediate","RightThumbDistal","RightIndexProximal","RightIndexIntermediate","RightIndexDistal","RightMiddleProximal","RightMiddleIntermediate","RightMiddleDistal","RightRingProximal","RightRingIntermediate","RightRingDistal","RightLittleProximal","RightLittleIntermediate","RightLittleDistal","UpperChest"};
    return bone<HumanBone::Count?names[static_cast<unsigned>(bone)]:"Invalid";
}
inline bool IsRequiredHumanBone(HumanBone bone) {
    return bone==HumanBone::Hips||bone==HumanBone::Spine||bone==HumanBone::Head
        ||(bone>=HumanBone::LeftUpperLeg&&bone<=HumanBone::RightFoot)
        ||(bone>=HumanBone::LeftUpperArm&&bone<=HumanBone::RightHand);
}
inline int HumanBoneParent(HumanBone bone) {
    static const int parents[]={-1,0,0,1,2,3,4,0,7,54,9,54,54,11,12,13,14,15,16,5,6,10,10,10,
        17,24,25,17,27,28,17,30,31,17,33,34,17,36,37,18,39,40,18,42,43,18,45,46,18,48,49,18,51,52,8};
    return bone<HumanBone::Count?parents[static_cast<unsigned>(bone)]:-1;
}
namespace HumanMath {
inline bool Finite(Vector3 v){return std::isfinite(v.X)&&std::isfinite(v.Y)&&std::isfinite(v.Z);}
inline Vector3 Multiply(Vector3 a,Vector3 b){return {a.X*b.X,a.Y*b.Y,a.Z*b.Z};}
inline Quaternion Inverse(Quaternion q){q.TryNormalize(q);return {-q.X,-q.Y,-q.Z,q.W};}
inline Quaternion Product(Quaternion a,Quaternion b){Quaternion q{a.W*b.X+a.X*b.W+a.Y*b.Z-a.Z*b.Y,a.W*b.Y-a.X*b.Z+a.Y*b.W+a.Z*b.X,a.W*b.Z+a.X*b.Y-a.Y*b.X+a.Z*b.W,a.W*b.W-a.X*b.X-a.Y*b.Y-a.Z*b.Z};q.TryNormalize(q);return q;}
inline Vector3 Rotate(Quaternion q,Vector3 p){q.TryNormalize(q);const Vector3 v{q.X,q.Y,q.Z};return p+v.Cross(p)*(2*q.W)+v.Cross(v.Cross(p))*2;}
inline Quaternion Between(Vector3 a,Vector3 b){if(!a.TryNormalize(a)||!b.TryNormalize(b))return {};const float dot=std::clamp(a.Dot(b),-1.f,1.f);if(dot<-.99999f){Vector3 axis=a.Cross(std::fabs(a.X)<.8f?Vector3{1,0,0}:Vector3{0,1,0});axis.TryNormalize(axis);return {axis.X,axis.Y,axis.Z,0};}const auto cross=a.Cross(b);Quaternion q{cross.X,cross.Y,cross.Z,1+dot};q.TryNormalize(q);return q;}
inline Quaternion Basis(Vector3 x,Vector3 y,Vector3 z){
    Quaternion q;const float trace=x.X+y.Y+z.Z;
    if(trace>0){const float s=2*std::sqrt(trace+1);q={(y.Z-z.Y)/s,(z.X-x.Z)/s,(x.Y-y.X)/s,s/4};}
    else if(x.X>y.Y&&x.X>z.Z){const float s=2*std::sqrt(1+x.X-y.Y-z.Z);q={s/4,(y.X+x.Y)/s,(z.X+x.Z)/s,(y.Z-z.Y)/s};}
    else if(y.Y>z.Z){const float s=2*std::sqrt(1+y.Y-x.X-z.Z);q={(y.X+x.Y)/s,s/4,(z.Y+y.Z)/s,(z.X-x.Z)/s};}
    else{const float s=2*std::sqrt(1+z.Z-x.X-y.Y);q={(z.X+x.Z)/s,(z.Y+y.Z)/s,s/4,(x.Y-y.X)/s};}q.TryNormalize(q);return q;
}
inline bool World(const SkeletonData& skeleton,const std::vector<TransformPose>& local,std::vector<TransformPose>& world,std::string& error){
    if(local.size()!=skeleton.Nodes.size()){error="Reference pose must contain every Skeleton node";return false;}
    world.resize(local.size());std::vector<unsigned char> colors(local.size());
    for(std::size_t i=0;i<local.size();++i){std::vector<std::size_t> chain;int at=int(i);
        while(at>=0&&at<int(local.size())&&!colors[at]){colors[at]=1;chain.push_back(at);at=skeleton.Nodes[at].Parent;}
        if(at< -1||at>=int(local.size())||(at>=0&&colors[at]==1)){error="Invalid humanoid Skeleton hierarchy";return false;}
        while(!chain.empty()){const auto node=chain.back();chain.pop_back();const auto& value=local[node];Quaternion q;
            if(!Finite(value.Position)||!Finite(value.Scale)||!value.Rotation.TryNormalize(q)||value.Scale.X<=0||value.Scale.Y<=0||value.Scale.Z<=0||std::fabs(value.Scale.X-value.Scale.Y)>1e-4f*value.Scale.X||std::fabs(value.Scale.X-value.Scale.Z)>1e-4f*value.Scale.X){error="Human transforms require finite rotations and positive uniform scale";return false;}
            const int parent=skeleton.Nodes[node].Parent;world[node]=value;
            if(parent>=0){const auto& p=world[parent];world[node].Position=p.Position+Rotate(p.Rotation,Multiply(p.Scale,value.Position));world[node].Rotation=Product(p.Rotation,value.Rotation);world[node].Scale=Multiply(p.Scale,value.Scale);}
            colors[node]=2;
        }
    }return true;
}
inline std::array<int,static_cast<unsigned>(HumanBone::Count)> Indices(const SkeletonData& skeleton,const HumanoidAvatarData& avatar){
    std::array<int,static_cast<unsigned>(HumanBone::Count)> indices;indices.fill(-1);std::map<std::string,int> paths;
    for(std::size_t i=0;i<skeleton.Nodes.size();++i)paths[skeleton.Nodes[i].Path]=int(i);
    for(const auto& mapping:avatar.Bones)if(mapping.Bone<HumanBone::Count){const auto found=paths.find(mapping.Path);if(found!=paths.end())indices[static_cast<unsigned>(mapping.Bone)]=found->second;}return indices;
}
}
inline bool ValidateHumanAvatar(const SkeletonData& skeleton,const HumanoidAvatarData& avatar,std::string& error){
    if(avatar.Skeleton.Guid.empty()||!avatar.Skeleton.LocalID||skeleton.Nodes.empty()||skeleton.Nodes.size()>100000||avatar.Bones.size()>static_cast<unsigned>(HumanBone::Count)
       ||!std::isfinite(avatar.HumanScale)||avatar.HumanScale<=1e-6f){error="Invalid human Avatar declaration";return false;}
    std::set<std::string> paths;for(const auto& node:skeleton.Nodes)if(node.Path.empty()||!paths.insert(node.Path).second){error="Duplicate human Skeleton path";return false;}
    std::set<HumanBone> roles;std::set<std::string> mapped;
    for(const auto& bone:avatar.Bones){Quaternion q;if(bone.Bone>=HumanBone::Count||!roles.insert(bone.Bone).second||!mapped.insert(bone.Path).second||!paths.count(bone.Path)||!bone.Axis.TryNormalize(q)){error="Invalid, duplicate or missing human bone mapping";return false;}}
    const auto indices=HumanMath::Indices(skeleton,avatar);
    for(unsigned role=0;role<static_cast<unsigned>(HumanBone::Count);++role){if(IsRequiredHumanBone(HumanBone(role))&&indices[role]<0){error=std::string("Missing required human bone: ")+HumanBoneName(HumanBone(role));return false;}
        if(indices[role]<0)continue;int expected=HumanBoneParent(HumanBone(role));while(expected>=0&&indices[expected]<0)expected=HumanBoneParent(HumanBone(expected));
        if(expected>=0){int parent=skeleton.Nodes[indices[role]].Parent;std::size_t guard=0;while(parent>=0&&parent<int(skeleton.Nodes.size())&&parent!=indices[expected]&&guard++<skeleton.Nodes.size())parent=skeleton.Nodes[parent].Parent;
            if(parent!=indices[expected]){error=std::string("Invalid human ancestry: ")+HumanBoneName(HumanBone(role));return false;}}
    }
    std::vector<TransformPose> world;return HumanMath::World(skeleton,avatar.ReferencePose,world,error);
}
inline bool BuildHumanAvatarAxes(const SkeletonData& skeleton,HumanoidAvatarData& avatar,std::string& error){
    // Start with identity axes, validate all mappings before deriving from geometry.
    for(auto& bone:avatar.Bones)bone.Axis={};avatar.HumanScale=1;
    if(!ValidateHumanAvatar(skeleton,avatar,error))return false;
    std::vector<TransformPose> world;if(!HumanMath::World(skeleton,avatar.ReferencePose,world,error))return false;
    const auto indices=HumanMath::Indices(skeleton,avatar);
    const auto position=[&](HumanBone bone){return world[indices[static_cast<unsigned>(bone)]].Position;};
    Vector3 up=position(HumanBone::Head)-position(HumanBone::Hips),right=position(HumanBone::RightUpperArm)-position(HumanBone::LeftUpperArm),forward;
    if(!up.TryNormalize(up)||!right.TryNormalize(right)||!right.Cross(up).TryNormalize(forward)){error="Human reference pose has degenerate body axes";return false;}
    const float torso=(position(HumanBone::Head)-position(HumanBone::Hips)).Length();
    const float leg=std::max((position(HumanBone::LeftFoot)-position(HumanBone::Hips)).Length(),(position(HumanBone::RightFoot)-position(HumanBone::Hips)).Length());
    avatar.HumanScale=torso+leg;if(avatar.HumanScale<1e-6f){error="Human reference height is zero";return false;}
    for(auto& bone:avatar.Bones){const unsigned role=static_cast<unsigned>(bone.Bone);const int index=indices[role];Vector3 direction;
        int child=-1;
        // Prefer anatomical continuation, never whichever sibling has the lowest enum value.
        const auto prefer=[&](std::initializer_list<HumanBone> candidates){for(auto candidate:candidates)if(indices[unsigned(candidate)]>=0){child=indices[unsigned(candidate)];return;}};
        switch(bone.Bone){
        case HumanBone::Hips:prefer({HumanBone::Spine});break;
        case HumanBone::Spine:prefer({HumanBone::Chest,HumanBone::UpperChest,HumanBone::Neck,HumanBone::Head});break;
        case HumanBone::Chest:prefer({HumanBone::UpperChest,HumanBone::Neck,HumanBone::Head});break;
        case HumanBone::UpperChest:prefer({HumanBone::Neck,HumanBone::Head});break;
        case HumanBone::Neck:prefer({HumanBone::Head});break;
        case HumanBone::LeftHand:prefer({HumanBone::LeftMiddleProximal});break;
        case HumanBone::RightHand:prefer({HumanBone::RightMiddleProximal});break;
        default:break;
        }
        if(child<0&&bone.Bone!=HumanBone::Head&&bone.Bone!=HumanBone::LeftHand&&bone.Bone!=HumanBone::RightHand)for(unsigned candidate=0;candidate<indices.size();++candidate){if(indices[candidate]<0)continue;int parent=HumanBoneParent(HumanBone(candidate));while(parent>=0&&indices[parent]<0)parent=HumanBoneParent(HumanBone(parent));if(parent==int(role)){child=indices[candidate];break;}}
        if(child>=0)direction=world[child].Position-world[index].Position;
        else{int parent=HumanBoneParent(bone.Bone);while(parent>=0&&indices[parent]<0)parent=HumanBoneParent(HumanBone(parent));direction=parent>=0?world[index].Position-world[indices[parent]].Position:up;}
        Vector3 x,z,y;if(!direction.TryNormalize(x)){error=std::string("Zero-length human segment: ")+HumanBoneName(bone.Bone);return false;}
        z=forward-x*forward.Dot(x);if(!z.TryNormalize(z)){z=up-x*up.Dot(x);if(!z.TryNormalize(z)){error="Degenerate human bone axis";return false;}}
        y=z.Cross(x);y.TryNormalize(y);bone.Axis=HumanMath::Basis(x,y,z);
    }return ValidateHumanAvatar(skeleton,avatar,error);
}
}
