#pragma once
#include "GameSproutEditorApi.h"
#include "GameSproutRuntimeApi.h"

class IAnimationPreviewListener {
public:
    virtual ~IAnimationPreviewListener() = default;
    // 主线程；仅场景播放模式改变时通知，单 Clip 使用下面的完整状态事件。
    virtual void OnAnimationPreviewStateChanged(bool scenePlaying) = 0;
    // 主线程，借用本次完整快照；切换/停止时先给旧对象发送空状态。
    virtual void OnClipPreviewStateChanged(uint64_t objectID,
        const GameSproutRuntime::AnimationClipPlaybackState& state) {}
    // 编辑姿态已还原；即便原先已经暂停，仍需要刷新被动画驱动的只读字段。
    virtual void OnAnimationPreviewReset() {}
};

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
    // 最近已发布的主线程快照；只用于订阅后的初始读取，不轮询 Runtime。
    virtual GameSproutRuntime::AnimationClipPlaybackState GetAnimationClipPreviewState(uint64_t objectID) const = 0;
    // 不回放历史，不持有监听者；先注册再读取一次初始状态，销毁前注销。
    virtual bool RegisterPreviewListener(IAnimationPreviewListener*) = 0;
    virtual void UnregisterPreviewListener(IAnimationPreviewListener*) = 0;
};
