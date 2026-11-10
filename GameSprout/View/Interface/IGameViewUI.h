#ifndef __IGameViewUI_h__
#define __IGameViewUI_h__

#include "GameSproutEditorApi.h"
#include <functional>

class IGameViewUI : public GameSproutEditor::IGameSproutView {
public:
    virtual ~IGameViewUI() {}
    using fViewportSizeCallback = std::function<void(GameSproutRuntime::Vector2)>;
    // Render target pixels. Zero means no output size has been established.
    virtual GameSproutRuntime::Vector2 GetViewportSize() const = 0;
    virtual bool RegisterViewportSizeCallback(const void* owner, const fViewportSizeCallback&) = 0;
    virtual void UnregisterViewportSizeCallback(const void* owner) = 0;
};

#endif //__IGameViewUI_h__
