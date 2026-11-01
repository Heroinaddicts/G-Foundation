#pragma once
#include "GameSproutEditorApi.h"
#include "GameSproutRuntimeApi.h"

struct RuntimeFrameEvent {
    uint64_t FrameNumber = 0;
    float DeltaSeconds = 0;
    bool SimulationPlaying = false;
};

class IRuntimeFrameListener {
public:
    virtual ~IRuntimeFrameListener() = default;
    // 宿主主线程：Runtime Update 已完成，可刷新本帧视口或当前播放信息。
    virtual void OnRuntimeFrame(const RuntimeFrameEvent&) = 0;
    virtual void OnSimulationPlayingChanged(bool) {}
};

// Editor 的 Runtime 更新服务。使用 EditorApi 注入的运行实例，
// 只管理资源提供者、时钟和唯一每帧 Update，不创建或 delete Runtime。
class IRuntimeHost : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~IRuntimeHost() = default;
    // 仅决定是否推进时间；暂停仍执行 Update(0) 以处理待派发运行事件。
    virtual void SetSimulationPlaying(bool playing) = 0;
    virtual bool IsSimulationPlaying() const = 0;
    // 开始、定位时间或切换预览后重置计时，避免把资源操作耗时带入动画。
    virtual void ResetClock() = 0;
    // 先订阅再读取一次初始状态；不回放历史，不持有监听者。
    // 播放面板可仅在活动期间订阅，停用/销毁前注销；回调内可安全注销。
    virtual bool RegisterFrameListener(IRuntimeFrameListener*) = 0;
    virtual void UnregisterFrameListener(IRuntimeFrameListener*) = 0;
};
