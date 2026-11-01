#pragma once
#include "IAnimationPreview.h"
#include "ISceneManager.h"
#include "IRuntimeHost.h"

class AnimationPreview final : public IAnimationPreview,public ISceneListener,public GameSproutRuntime::IRuntimeListener {
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
    void OnActiveSceneChanged() override;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent&) override;
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
    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr; // Borrowed from EditorApi.
    IRuntimeHost* _Host=nullptr;
    ISceneManager* _Scenes=nullptr;
    uint64_t _ClipObjectID=0;
    bool _Playing=false,_SceneSubscribed=false,_RuntimeSubscribed=false;
};
