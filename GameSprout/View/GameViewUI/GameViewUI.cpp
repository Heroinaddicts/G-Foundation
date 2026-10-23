#include "GameViewUI.h"
#include "IIDEWindow.h"
#include "ui_GameViewUI.h"
#include <QApplication>
#include <QMainWindow>

void GameViewUI::BeforeInitialize(GameSprout::GameSproutEditor * const editor) {
    g_Self = this;
    g_GameSproutEditor = editor;
    GetGameSproutView(editor, g_IIDEWindow, IDEWindow);

    GSRegLaunchedCallback(g_IIDEWindow, [&](GameSprout::GameSproutEditor * const editor) {
        return nullptr != g_IIDEWindow->CreateDocker(QStringLiteral("Game"));
    });
}

bool GameViewUI::Initialize(GameSprout::GameSproutEditor * const editor) {
    return true;
}

bool GameViewUI::Launch(GameSprout::GameSproutEditor * const editor) {
    return true;
}

void GameViewUI::LaunchFinished(GameSprout::GameSproutEditor * const editor) {
    Q_UNUSED(editor);
}

void GameViewUI::Show() {
}

void GameViewUI::Hide() {
}

void GameViewUI::Update(GameSprout::GameSproutEditor * const editor) {
}

void GameViewUI::Release(GameSprout::GameSproutEditor * const editor) {
}
