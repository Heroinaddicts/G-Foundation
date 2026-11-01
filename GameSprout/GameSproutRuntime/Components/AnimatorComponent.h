#pragma once
#include "BuiltinComponent.h"
#include <map>
#include <set>
namespace GameSproutRuntime {
class Runtime;
class AnimatorComponent final : public BuiltinComponent<IAnimatorComponent> {
public:
    explicit AnimatorComponent(IGameObject*);
    void Update(IGameSproutRuntime*,float) override;
    void OnRelease(IGameSproutRuntime*) override;
    ResourceReference GetAvatar() const override {return _Avatar;}
    void SetAvatar(const ResourceReference&) override;
    bool SetIKGoal(AvatarIKGoal,const AnimatorIKGoal&) override;
    AnimatorIKGoal GetIKGoal(AvatarIKGoal) const override;
    bool SetLookAt(const AnimatorLookAt&) override;
    AnimatorLookAt GetLookAt() const override {return _LookAt;}
    void ClearIKGoals() override;
    ResourceReference GetSkeleton() const override {return _Skeleton;}
    void SetSkeleton(const ResourceReference&) override;
    ResourceReference GetController() const override {return _Controller;}
    void SetController(const ResourceReference&) override;
    ResourceReference GetDefaultClip() const override {return _DefaultClip;}
    void SetDefaultClip(const ResourceReference&) override;
    const std::vector<AnimationNodeBinding>& GetNodeBindings() const override {return _Bindings;}
    bool SetNodeBindings(const std::vector<AnimationNodeBinding>&) override;
    float GetSpeed() const override {return _Speed;}
    bool SetSpeed(float) override;
    bool GetPlayOnStart() const override {return _PlayOnStart;}
    void SetPlayOnStart(bool) override;
    bool GetLoop() const override {return _Loop;}
    void SetLoop(bool) override;
    bool SetFloat(const std::string&,float) override;
    bool SetInt(const std::string&,std::int32_t) override;
    bool SetBool(const std::string&,bool) override;
    bool SetTrigger(const std::string&) override;
    bool ResetTrigger(const std::string&) override;
    bool GetParameter(const std::string&,double&) const override;
    std::uint32_t GetLayerCount() const override;
    std::string GetLayerName(std::uint32_t) const override;
    float GetLayerWeight(std::uint32_t) const override;
    bool SetLayerWeight(std::uint32_t,float) override;
    bool PlayClip(const ResourceReference&,float,float,bool) override;
    bool SetClipPlaybackPlaying(bool) override;
    bool SeekClip(float) override;
    void StopClip() override;
    AnimationClipPlaybackState GetClipPlaybackState() const override {return _ClipPlayback;}
    bool Play(const std::string&,float,std::uint32_t) override;
    bool CrossFade(const std::string&,float,float,std::uint32_t) override;
    void Pause() override {_Playing=false;_Stopped=true;_ClipPlayback.Playing=false;_RootMotionDelta={};++_SignalVersion;}
    void Resume() override;
    void Stop() override;
    bool Seek(float,std::uint32_t) override;
    AnimatorPlaybackState GetPlaybackState(std::uint32_t) const override;
    const std::string& GetLastError() const override {return _Error;}
    bool ResolveBone(const std::string&,std::uint64_t&) const override;
    void ForgetObject(std::uint64_t);
    RootMotionMode GetRootMotionMode() const override {return _RootMotionMode;}
    bool SetRootMotionMode(RootMotionMode) override;
    RootMotionDelta GetRootMotionDelta() const override {return _RootMotionDelta;}
    std::uint64_t GetSignalVersion() const {return _SignalVersion;}
    bool ApplyRootMotion(const RootMotionDelta&);
    bool ApplyRootMotionDelta(const RootMotionDelta&) override;
    void BeginRootMotionDispatch();
    bool EndRootMotionDispatch(RootMotionDelta& solved);
    void MotionFailure(const std::string& error);

private:
    Runtime& Owner() const;
    void ConfigurationChanged();
    bool Ensure();
    void ClearPoses(bool retainClipPose=false);
    bool ApplyClipPlayback(AnimationClipPlaybackState&);
    void UpdateClipPlayback(float);
    bool ApplyPose();
    bool ApplyHumanIK(std::vector<TransformPose>&,bool callbacks);
    bool SampleClip(const AnimationClipData&,double,bool,std::vector<TransformPose>&,std::vector<float>&);
    bool RetargetRoot(const AnimationClipData&,RootMotionDelta&);
    float RetargetScale(const AnimationClipData&);
    std::uint32_t SourceLayer(std::uint32_t) const;
    void SynchronizeLayers(std::uint32_t);
    double RawDuration(std::uint32_t,int);
    bool _DispatchIK=false,_IKAborted=false;
    AnimatorIKGoal _IKGoals[4];
    AnimatorLookAt _LookAt;
    bool StatePose(std::uint32_t,int,double,std::vector<TransformPose>&,
                   const std::vector<TransformPose>* base=nullptr,std::vector<float>* coverage=nullptr);
    bool InitializeLayers();
    bool LayerPose(std::uint32_t,const std::vector<TransformPose>&,std::vector<TransformPose>&);
    bool UpdateLayer(std::uint32_t,float);
    double Duration(std::uint32_t,int);
    bool SetParameter(const std::string&,AnimatorParameterType,double);
    int StateIndex(std::uint32_t,const std::string&) const;
    bool StartTransition(std::uint32_t,int,float,float);
    bool Conditions(const AnimatorTransition&) const;
    bool EvaluateTransitions(std::uint32_t,double previousNormalized,double currentNormalized);
    struct ActiveMotion {ResourceReference Clip;double Previous=0,Current=0;float BeginWeight=1,EndWeight=1;int State=-1;};
    bool StateMotions(std::uint32_t,int,double,double,float,float,std::vector<ActiveMotion>&);
    bool EvaluateMotionSignals(std::uint32_t,const std::vector<ActiveMotion>&);
    void EnterState(std::uint32_t,int);
    void ExitState(std::uint32_t,int);
    void ExitStates(std::uint32_t);
    RootMotionMode _RootMotionMode=RootMotionMode::Disabled;
    RootMotionDelta _RootMotionDelta,_AccumulatedMotion,_SolvedMotion;
    bool _RootDispatchActive=false,_RootDispatchSubmitted=false;
    std::uint64_t _RootDispatchVersion=0;
    std::uint64_t _SignalVersion=1;
    struct LayerInstance {
        int Current=-1,Next=-1;
        double Time=0,NextTime=0;
        float Weight=1,RootMotionWeight=1,TransitionSeconds=0,TransitionElapsed=0;
        bool Frozen=false,HasRootMotion=false;
        std::vector<TransformPose> FrozenPose,Pose,ReferencePose;
        std::vector<float> MaskWeights,PoseCoverage,FrozenCoverage;
        std::set<int> EnteredStates,StartMarkers;
        RootMotionDelta MotionDelta;
    };
    std::vector<LayerInstance> _Layers;
    std::set<std::string> _ConsumedTriggers;
    std::size_t _MotionEventCount=0;
    ResourceReference _Avatar,_Skeleton,_Controller,_DefaultClip;
    const HumanoidAvatarData* _AvatarData=nullptr;
    std::vector<AnimationNodeBinding> _Bindings;
    std::vector<std::uint64_t> _Applied;
    std::vector<TransformPose> _Pose;
    std::map<std::string,double> _Parameters;
    const SkeletonData* _SkeletonData=nullptr;
    const AnimatorControllerData* _ControllerData=nullptr;
    AnimatorControllerData _Fallback;
    std::uint64_t _Generation=0;
    float _Speed=1;
    bool _PlayOnStart=true,_Loop=true,_Playing=false,_Stopped=false;
    AnimationClipPlaybackState _ClipPlayback;
    std::string _Error;
};
}
