#include "SceneViewUI.h"
#include "IIDEWindow.h"
#include "ui_SceneViewUI.h"
#include <QApplication>
#include <QMainWindow>

void SceneViewUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi * const editor) {
    g_Self = this;
    g_GameSproutEditor = editor;
    GetGameSproutView(editor, g_IIDEWindow, IDEWindow);

    GSRegLaunchedCallback(g_IIDEWindow, [&](GameSproutEditor::GameSproutViewApi * const editor) {
        return nullptr != g_IIDEWindow->CreateDocker(QStringLiteral("Scene"));
    });
}

bool SceneViewUI::Initialize(GameSproutEditor::GameSproutViewApi * const editor) {
    return true;
}

bool SceneViewUI::Launch(GameSproutEditor::GameSproutViewApi * const editor) {
    return true;
}

void SceneViewUI::LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) {
}

void SceneViewUI::Show() {
}

void SceneViewUI::Hide() {
}

void SceneViewUI::Update(GameSproutEditor::GameSproutViewApi * const editor) {
}

void SceneViewUI::Release(GameSproutEditor::GameSproutViewApi * const editor) {
}
