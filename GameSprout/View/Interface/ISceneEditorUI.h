#pragma once
#include "GameSproutEditorApi.h"

class ISceneEditorUI : public GameSproutEditor::IGameSproutView {
public:
    virtual ~ISceneEditorUI() = default;
    virtual void SaveScene() = 0;
    virtual void SaveSceneAs() = 0;
    // 交互入口：未保存时提供保存、放弃和取消；底层同步加载仍由 SceneManager 执行。
    virtual void OpenSceneAsset(const std::string& relativePath) = 0;
};
