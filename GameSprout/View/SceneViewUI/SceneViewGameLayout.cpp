#include "SceneViewUI.h"
#include "SceneViewport.h"
#include "IGameViewUI.h"

void SceneViewUI::BindGameUILayout(GameSproutEditor::GameSproutViewApi* editor) {
    UnbindGameUILayout();
    if (!editor || !_Viewport) return;
    _GameView = editor->GetGameSproutView<IGameViewUI>("GameViewUI");
    if (!_GameView) return;
    _GameView->RegisterViewportSizeCallback(this, [this](GameSproutRuntime::Vector2 size) {
        if (_Viewport) _Viewport->SetGameUILayoutSize(size);
    });
    _Viewport->SetGameUILayoutSize(_GameView->GetViewportSize());
}

void SceneViewUI::UnbindGameUILayout() {
    if (_GameView) _GameView->UnregisterViewportSizeCallback(this);
    _GameView = nullptr;
}
