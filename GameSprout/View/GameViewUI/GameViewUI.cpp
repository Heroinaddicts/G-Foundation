#include "GameViewUI.h"
#include "IIDEWindow.h"
#include "ui_GameViewUI.h"
#include <QApplication>
#include <QMainWindow>

void GameViewUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi * const editor) {
    g_Self = this;
    g_GameSproutEditor = editor;
    GetGameSproutView(editor, g_IIDEWindow, IDEWindow);

    GSRegLaunchedCallback(g_IIDEWindow, [&](GameSproutEditor::GameSproutViewApi * const editor) {
        return nullptr != g_IIDEWindow->CreateDocker(QStringLiteral("Game"));
    });
}

bool GameViewUI::Initialize(GameSproutEditor::GameSproutViewApi * const editor) {
    return true;
}

bool GameViewUI::Launch(GameSproutEditor::GameSproutViewApi * const editor) {
    return true;
}

void GameViewUI::LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) {
    Q_UNUSED(editor);
}

void GameViewUI::Show() {
}

void GameViewUI::Hide() {
}

void GameViewUI::Update(GameSproutEditor::GameSproutViewApi * const editor) {
}

void GameViewUI::Release(GameSproutEditor::GameSproutViewApi * const editor) {
}
