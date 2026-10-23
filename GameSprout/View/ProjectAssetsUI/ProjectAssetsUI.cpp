#include "ProjectAssetsUI.h"
#include "IIDEWindow.h"
#include "ui_ProjectAssetsUI.h"
#include <QApplication>
#include <QMainWindow>

void ProjectAssetsUI::BeforeInitialize(GameSprout::GameSproutEditor * const editor) {
    g_Self = this;
    g_GameSproutEditor = editor;
    GetGameSproutView(editor, g_IIDEWindow, IDEWindow);
    GSRegLaunchedCallback(g_IIDEWindow, [&](GameSprout::GameSproutEditor * const editor) {
        return nullptr != g_IIDEWindow->CreateDocker(QStringLiteral("ProjectAssets"));
    });
}

bool ProjectAssetsUI::Initialize(GameSprout::GameSproutEditor * const editor) {
    return true;
}

bool ProjectAssetsUI::Launch(GameSprout::GameSproutEditor * const editor) {
    return true;
}

void ProjectAssetsUI::LaunchFinished(GameSprout::GameSproutEditor * const editor) {
}

void ProjectAssetsUI::Show() {
}

void ProjectAssetsUI::Hide() {
}

void ProjectAssetsUI::Update(GameSprout::GameSproutEditor * const editor) {
}

void ProjectAssetsUI::Release(GameSprout::GameSproutEditor * const editor) {
}
