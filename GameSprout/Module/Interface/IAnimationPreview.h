#pragma once
#include "GameSproutEditorApi.h"
#include "GameSproutRuntimeApi.h"

// Editor 临时预览服务，不是运行时 Animator，也不拥有 Runtime 或场景文件。
class IAnimationPreview : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~IAnimationPreview() = default;
    // 整个活动场景按各 Animator 的配置播放；启动时结束单 Clip 预览。
    virtual void SetAnimationPreviewPlaying(bool playing) = 0;
    virtual bool IsAnimationPreviewPlaying() const = 0;
    virtual void ResetAnimationPreview() = 0;
    // 单对象 Clip 直播放。不改 Controller、默认 Clip、场景作者数据或撤销历史。
    virtual bool PlayAnimationClip(uint64_t objectID,const GameSproutRuntime::ResourceReference& clip,
        float seconds,float speed,bool loop,std::string& error) = 0;
    virtual bool SetAnimationClipPreviewPlaying(uint64_t objectID,bool playing,std::string& error) = 0;
    virtual bool SeekAnimationClipPreview(uint64_t objectID,float seconds,std::string& error) = 0;
    virtual void StopAnimationClipPreview(uint64_t objectID) = 0;
    virtual GameSproutRuntime::AnimationClipPlaybackState GetAnimationClipPreviewState(uint64_t objectID) const = 0;
};
