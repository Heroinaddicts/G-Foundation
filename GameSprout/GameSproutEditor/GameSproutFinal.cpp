#include "GameSproutFinal.h"
#include "GameSproutEditorFinal.h"

namespace GameSproutEditor {
    GameSproutFinal * GameSproutFinal::GetInstance() {
        return GameSproutEditorFinal::GetInstance()->_Api;
    }

    void GameSproutFinal::Update() {
        _Foundation->Update();
        static_cast<GameSproutEditorFinal*>(_EditorApi)->Update();
    }
}
