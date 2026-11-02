#pragma once
#include "GameSproutEditorApi.h"
#include <functional>

class ISceneEditorUI : public GameSproutEditor::IGameSproutView {
public:
    virtual ~ISceneEditorUI() = default;
    virtual void SaveScene() = 0;
    virtual void SaveSceneAs() = 0;
    // 交互入口：未保存时提供保存、放弃和取消；底层同步加载仍由 SceneManager 执行。
    virtual void OpenSceneAsset(const std::string& relativePath) = 0;
    // Feature Views route the active external document without exposing its
    // format to the scene menu. Callbacks are borrowed until owner unregisters.
    using fDocumentActive=std::function<bool()>;
    using fDocumentSave=std::function<void()>;
    using fDocumentClose=std::function<void(const std::function<void()>&)>;
    virtual bool RegisterDocumentHandler(const void*,const fDocumentActive&,const fDocumentSave&,const fDocumentClose&) {return false;}
    virtual void UnregisterDocumentHandler(const void*) {}
    virtual void RefreshDocumentActions() {}
};
