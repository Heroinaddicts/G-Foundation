#include "AnimatorComponent.h"
#include "GameObject.h"
#include "Api/HumanoidValidation.h"
#include "Animation/AnimationSampling.h"
#include <cmath>
namespace GameSproutRuntime {
namespace {
bool Weight(float value){return std::isfinite(value)&&value>=0&&value<=1;}
Quaternion Mix(Quaternion a,Quaternion b,float weight){TransformPose x,y;x.Rotation=a;y.Rotation=b;return AnimationSampling::Blend(x,y,weight).Rotation;}
bool World(const SkeletonData& skeleton,const std::vector<TransformPose>& poses,std::vector<TransformPose>& result,std::string& error){return HumanMath::World(skeleton,poses,result,error);}
void SetWorldRotation(const SkeletonData& skeleton,std::vector<TransformPose>& poses,const std::vector<TransformPose>& world,int node,Quaternion rotation){const int parent=skeleton.Nodes[node].Parent;poses[node].Rotation=parent<0?rotation:HumanMath::Product(HumanMath::Inverse(world[parent].Rotation),rotation);}
bool Limb(const SkeletonData& skeleton,std::vector<TransformPose>& poses,int upper,int lower,int end,const AnimatorIKGoal& goal,std::string& error){
    std::vector<TransformPose> world;if(!World(skeleton,poses,world,error))return false;
    const auto root=world[upper].Position,mid=world[lower].Position,tip=world[end].Position;
    const float a=(mid-root).Length(),b=(tip-mid).Length();if(a<1e-6f||b<1e-6f){error="IK limb contains a zero-length segment";return false;}
    if(goal.PositionWeight>0){
        const auto desired=tip*(1-goal.PositionWeight)+goal.Position*goal.PositionWeight;
        Vector3 direction=desired-root;if(!direction.TryNormalize(direction))direction=(tip-root);if(!direction.TryNormalize(direction))direction={0,1,0};
        const float distance=std::clamp((desired-root).Length(),std::fabs(a-b)+1e-6f,std::max(std::fabs(a-b)+1e-6f,a+b-1e-6f));
        Vector3 bend=mid-root-direction*(mid-root).Dot(direction);if(!bend.TryNormalize(bend)){bend=direction.Cross(std::fabs(direction.X)<.8f?Vector3{1,0,0}:Vector3{0,1,0});bend.TryNormalize(bend);}
        Vector3 hint=goal.Hint-root-direction*(goal.Hint-root).Dot(direction);
        if(goal.HintWeight>0&&hint.TryNormalize(hint)){bend=bend*(1-goal.HintWeight)+hint*goal.HintWeight;if(!bend.TryNormalize(bend))bend=hint;}
        const float along=(a*a+distance*distance-b*b)/(2*distance);const float height=std::sqrt(std::max(0.f,a*a-along*along));
        const auto newMid=root+direction*along+bend*height,newEnd=root+direction*distance;
        SetWorldRotation(skeleton,poses,world,upper,HumanMath::Product(HumanMath::Between(mid-root,newMid-root),world[upper].Rotation));
        if(!World(skeleton,poses,world,error))return false;
        SetWorldRotation(skeleton,poses,world,lower,HumanMath::Product(HumanMath::Between(world[end].Position-world[lower].Position,newEnd-world[lower].Position),world[lower].Rotation));
        if(!World(skeleton,poses,world,error))return false;
    }
    if(goal.RotationWeight>0)SetWorldRotation(skeleton,poses,world,end,Mix(world[end].Rotation,goal.Rotation,goal.RotationWeight));
    return true;
}
}
bool AnimatorComponent::SetIKGoal(AvatarIKGoal goal,const AnimatorIKGoal& value){
    Quaternion rotation;if(goal>=AvatarIKGoal::Count||!HumanMath::Finite(value.Position)||!HumanMath::Finite(value.Hint)||!value.Rotation.TryNormalize(rotation)
        ||!Weight(value.PositionWeight)||!Weight(value.RotationWeight)||!Weight(value.HintWeight))return false;
    if(value.PositionWeight>0||value.RotationWeight>0||value.HintWeight>0){if(_Avatar.Guid.empty()){_Error="IK goals require a Humanoid Avatar";return false;}if(!Ensure())return false;}
    _IKGoals[unsigned(goal)]=value;_IKGoals[unsigned(goal)].Rotation=rotation;if(!_DispatchIK&&!_Pose.empty()){if(!Ensure())return false;return ApplyPose();}return true;
}
AnimatorIKGoal AnimatorComponent::GetIKGoal(AvatarIKGoal goal)const{return goal<AvatarIKGoal::Count?_IKGoals[unsigned(goal)]:AnimatorIKGoal{};}
bool AnimatorComponent::SetLookAt(const AnimatorLookAt& value){if(!HumanMath::Finite(value.Position)||!Weight(value.Weight)||!Weight(value.BodyWeight)||!Weight(value.HeadWeight)||!Weight(value.EyesWeight)||!Weight(value.ClampWeight))return false;if(value.Weight>0){if(_Avatar.Guid.empty()){_Error="LookAt requires a Humanoid Avatar";return false;}if(!Ensure())return false;}_LookAt=value;if(!_DispatchIK&&!_Pose.empty()){if(!Ensure())return false;return ApplyPose();}return true;}
void AnimatorComponent::ClearIKGoals(){for(auto& goal:_IKGoals)goal={};_LookAt={};if(!_DispatchIK&&!_Pose.empty()&&Ensure())ApplyPose();}
bool AnimatorComponent::ApplyHumanIK(std::vector<TransformPose>& output,bool callbacks){
    _IKAborted=false;if(!_AvatarData)return true;const auto indices=HumanMath::Indices(*_SkeletonData,*_AvatarData);
    const HumanBone chains[4][3]={{HumanBone::LeftUpperLeg,HumanBone::LeftLowerLeg,HumanBone::LeftFoot},{HumanBone::RightUpperLeg,HumanBone::RightLowerLeg,HumanBone::RightFoot},{HumanBone::LeftUpperArm,HumanBone::LeftLowerArm,HumanBone::LeftHand},{HumanBone::RightUpperArm,HumanBone::RightLowerArm,HumanBone::RightHand}};
    for(std::uint32_t layer=0;layer<_Layers.size();++layer){if(!_ControllerData->Layers[layer].IKPass)continue;
        if(callbacks&&!Owner().DispatchAnimatorIK(*this,layer)){_IKAborted=true;return true;}
        if(_LookAt.Weight>0){std::vector<TransformPose> reference;if(!World(*_SkeletonData,_AvatarData->ReferencePose,reference,_Error))return false;
            for(const auto role:std::vector<std::pair<HumanBone,float>>{{HumanBone::Spine,_LookAt.BodyWeight},{HumanBone::Head,_LookAt.HeadWeight},{HumanBone::LeftEye,_LookAt.EyesWeight},{HumanBone::RightEye,_LookAt.EyesWeight}}){const int node=indices[unsigned(role.first)];if(node<0||role.second==0)continue;
                std::vector<TransformPose> world;if(!World(*_SkeletonData,output,world,_Error))return false;
                Quaternion axis;for(const auto& bone:_AvatarData->Bones)if(bone.Bone==HumanBone::Hips)axis=bone.Axis;
                const auto forward=HumanMath::Rotate(HumanMath::Product(HumanMath::Product(world[node].Rotation,HumanMath::Inverse(reference[node].Rotation)),axis),{0,0,1});
                Vector3 direction=_LookAt.Position-world[node].Position;if(!direction.TryNormalize(direction))continue;
                Quaternion delta=HumanMath::Between(forward,direction);const float angle=2*std::acos(std::clamp(std::fabs(delta.W),0.f,1.f));const float limit=(1-_LookAt.ClampWeight)*3.14159265f;
                const float blend=_LookAt.Weight*role.second*(angle>limit&&angle>1e-6f?limit/angle:1);
                SetWorldRotation(*_SkeletonData,output,world,node,HumanMath::Product(Mix({},delta,blend),world[node].Rotation));
            }
        }
        for(unsigned goal=0;goal<4;++goal){const auto& value=_IKGoals[goal];if(value.PositionWeight==0&&value.RotationWeight==0)continue;
            if(!Limb(*_SkeletonData,output,indices[unsigned(chains[goal][0])],indices[unsigned(chains[goal][1])],indices[unsigned(chains[goal][2])],value,_Error))return false;}
    }return true;
}
}
