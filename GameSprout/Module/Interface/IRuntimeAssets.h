#pragma once
#include "GameSproutEditorApi.h"
#include "RuntimeResourceApi.h"

class IRuntimeAssetsListener {
public:
    virtual ~IRuntimeAssetsListener() = default;
    // Main-thread value notification, only when an observed prepared resource
    // changes or a failed read becomes retryable after a new publication.
    virtual void OnRuntimeResourcesChanged(std::uint64_t revision) = 0;
};

// Editor 资源适配层：只读取 Library/ArtifactDB，供统一 Runtime 使用。
class IRuntimeAssets : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~IRuntimeAssets() = default;
    // 借用，模块 Release 前有效；与 Runtime 一样仅在宿主主线程调用。
    virtual GameSproutRuntime::IRuntimeResourceProvider* GetResourceProvider() const = 0;
    virtual bool RegisterResourceListener(IRuntimeAssetsListener*) = 0;
    virtual void UnregisterResourceListener(IRuntimeAssetsListener*) = 0;
};
