#pragma once
#include "GameSproutEditorApi.h"
#include "GameSproutRuntimeApi.h"

struct RuntimeFrameEvent {
    uint64_t FrameNumber = 0;
    float DeltaSeconds = 0;
    bool SimulationPlaying = false;
    bool GameplayActive = false;
    bool GameplayPaused = false;
};

class IRuntimeFrameListener {
public:
    virtual ~IRuntimeFrameListener() = default;
    // 宿主主线程：Runtime 全帧阶段（包括动画和 LateUpdate）已完成。
    // 两种视口共用本次结果，不再分别推进生命周期。
    virtual void OnRuntimeFrame(const RuntimeFrameEvent&) = 0;
    virtual void OnSimulationPlayingChanged(bool) {}
};

// Editor 的 Runtime 更新服务。使用 EditorApi 注入的运行实例，
// 只管理资源提供者、时钟和唯一每帧 AdvanceFrame，不创建或 delete Runtime。
class IRuntimeHost : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~IRuntimeHost() = default;
    // 编辑动画预览时钟；正式游戏会话期间忽略预览对时钟的更改。
    virtual void SetSimulationPlaying(bool playing) = 0;
    virtual bool IsSimulationPlaying() const = 0;
    // 主线程，禁止Runtime回调/帧通知重入。Begin完成脚本启动阶段后才返回。
    virtual bool BeginGameplay(std::string& error) {error="Gameplay execution is unavailable";return false;}
    virtual void EndGameplay() {}
    virtual void SetGameplayPaused(bool) {}
    // 仅暂停中接受，下一次Host帧推进1/60秒：Early、一次固定步、Update、动画、Late，随后仍暂停。
    virtual bool StepGameplay(std::string& error) {error="Gameplay stepping is unavailable";return false;}
    virtual bool IsGameplayActive() const {return false;}
    virtual bool IsGameplayPaused() const {return false;}
    // Includes frame listeners and Host transition notifications. Mode/world
    // replacement must wait until this boundary has returned to the caller.
    virtual bool IsFrameActive() const {return false;}
    // 开始、定位时间或切换预览后重置计时，避免把资源操作耗时带入动画。
    virtual void ResetClock() = 0;
    // 先订阅再读取一次初始状态；不回放历史，不持有监听者。
    // 播放面板可仅在活动期间订阅，停用/销毁前注销；回调内可安全注销。
    virtual bool RegisterFrameListener(IRuntimeFrameListener*) = 0;
    virtual void UnregisterFrameListener(IRuntimeFrameListener*) = 0;
};
