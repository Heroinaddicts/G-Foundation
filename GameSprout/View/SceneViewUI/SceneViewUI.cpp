#include "SceneViewUI.h"
#include "IIDEWindow.h"
#include "ui_SceneViewUI.h"
#include <QApplication>
#include <QMainWindow>

void SceneViewUI::BeforeInitialize(GameSprout::GameSproutEditor * const editor) {
    g_Self = this;
    g_GameSproutEditor = editor;
    GetGameSproutView(editor, g_IIDEWindow, IDEWindow);

    GSRegLaunchedCallback(g_IIDEWindow, [&](GameSprout::GameSproutEditor * const editor) {
        return nullptr != g_IIDEWindow->CreateDocker(QStringLiteral("Scene"));
    });
}

bool SceneViewUI::Initialize(GameSprout::GameSproutEditor * const editor) {
    return true;
}

bool SceneViewUI::Launch(GameSprout::GameSproutEditor * const editor) {
    return true;
}

void SceneViewUI::LaunchFinished(GameSprout::GameSproutEditor * const editor) {
}

void SceneViewUI::Show() {
}

void SceneViewUI::Hide() {
}

void SceneViewUI::Update(GameSprout::GameSproutEditor * const editor) {
}

void SceneViewUI::Release(GameSprout::GameSproutEditor * const editor) {
}
