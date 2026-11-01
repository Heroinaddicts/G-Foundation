#pragma once
#include "GameSproutEditorApi.h"
#include "GameSproutRuntimeApi.h"

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
};
