#pragma once
#include "IAnimationPreview.h"
#include "ISceneManager.h"
#include "IRuntimeHost.h"
#include <map>
#include <set>

class AnimationPreview final : public IAnimationPreview,public ISceneListener,public GameSproutRuntime::IRuntimeListener,public IRuntimeFrameListener {
public:
    ~AnimationPreview() override;
    void SetAnimationPreviewPlaying(bool playing) override;
    bool IsAnimationPreviewPlaying() const override {return _Playing;}
    void ResetAnimationPreview() override;
    bool PlayAnimationClip(uint64_t,const GameSproutRuntime::ResourceReference&,float,float,bool,std::string&) override;
    bool SetAnimationClipPreviewPlaying(uint64_t,bool,std::string&) override;
    bool SeekAnimationClipPreview(uint64_t,float,std::string&) override;
    void StopAnimationClipPreview(uint64_t) override;
    GameSproutRuntime::AnimationClipPlaybackState GetAnimationClipPreviewState(uint64_t) const override;
    bool RegisterPreviewListener(IAnimationPreviewListener*) override;
    void UnregisterPreviewListener(IAnimationPreviewListener*) override;
    void OnActiveSceneChanged() override;
    void OnSceneStateChanged() override;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent&) override;
    void OnRuntimeFrame(const RuntimeFrameEvent&) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override;
    void Update(GameSproutEditor::GameSproutModuleApi*) override;
    void Release(GameSproutEditor::GameSproutModuleApi*) override;
    GameSproutRuntime::IGameSproutRuntime* Runtime() const;
    bool Ready(std::string&) const;
    GameSproutRuntime::IAnimatorComponent* PreviewAnimator(uint64_t,bool currentScene=true) const;
    void ClearClip();
    void PauseOtherAnimators(uint64_t exceptObjectID);
    void UpdateClock();
    void PublishState(bool force=false);
    void PublishState(const GameSproutRuntime::AnimationClipPlaybackState&,bool force=false);
    GameSproutRuntime::AnimationClipPlaybackState CurrentClipState() const;
    void UpdateFrameSubscription();
    void NotifyReset();
    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr; // Borrowed from EditorApi.
    IRuntimeHost* _Host=nullptr;
    ISceneManager* _Scenes=nullptr;
    uint64_t _ClipObjectID=0;
    bool _Playing=false,_SceneSubscribed=false,_RuntimeSubscribed=false;
    bool _FrameSubscribed=false,_CheckClip=false;
    std::set<uint64_t> _PendingAnimators;
    std::map<uint64_t,IAnimationPreviewListener*> _Listeners;
    uint64_t _NextListener=0,_PublishedObjectID=0;
    uint64_t _StateRevision=0;
    bool _PublishedPlaying=false;
    GameSproutRuntime::AnimationClipPlaybackState _PublishedClip;
};
