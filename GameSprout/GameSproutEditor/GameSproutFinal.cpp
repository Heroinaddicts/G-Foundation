#include "GameSproutFinal.h"
#include "GameSproutEditorFinal.h"

namespace GameSproutEditor {
    GameSproutFinal * GameSproutFinal::GetInstance() {
        GameSproutEditorFinal* editor = GameSproutEditorFinal::GetInstance();
        return editor ? editor->_Api : nullptr;
    }

    void GameSproutFinal::Update() {
        _Foundation->Update();
        static_cast<GameSproutEditorFinal*>(_EditorApi)->Update();
    }
}
