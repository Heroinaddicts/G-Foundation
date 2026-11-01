#pragma once
#include "GameSproutEditorApi.h"
#include "RuntimeResourceApi.h"

// Editor 资源适配层：只读取 Library/ArtifactDB，供统一 Runtime 使用。
class IRuntimeAssets : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~IRuntimeAssets() = default;
    // 借用，模块 Release 前有效；与 Runtime 一样仅在宿主主线程调用。
    virtual GameSproutRuntime::IRuntimeResourceProvider* GetResourceProvider() const = 0;
};
