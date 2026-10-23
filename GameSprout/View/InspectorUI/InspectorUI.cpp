#include "InspectorUI.h"
#include "IIDEWindow.h"
#include "ui_InspectorUI.h"
#include <QApplication>
#include <QMainWindow>


void InspectorUI::BeforeInitialize(GameSprout::GameSproutEditor * const editor) {
    g_Self = this;
    g_GameSproutEditor = editor;
    GetGameSproutView(editor, g_IIDEWindow, IDEWindow);
    GSRegLaunchedCallback(g_IIDEWindow, [&](GameSprout::GameSproutEditor * const editor) {
        g_IIDEWindow->CreateDocker(QStringLiteral("Inspector"));
        return true;
    });
}

bool InspectorUI::Initialize(GameSprout::GameSproutEditor * const editor) {
    return true;
}

bool InspectorUI::Launch(GameSprout::GameSproutEditor * const editor) {
    return true;
}

void InspectorUI::LaunchFinished(GameSprout::GameSproutEditor * const editor) {
}

void InspectorUI::Show() {
}

void InspectorUI::Hide() {
}

void InspectorUI::Update(GameSprout::GameSproutEditor * const editor) {
}

void InspectorUI::Release(GameSprout::GameSproutEditor * const editor) {
}
