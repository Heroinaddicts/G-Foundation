#include "HierarchyUI.h"
#include "IIDEWindow.h"
#include "ui_HierarchyUI.h"
#include <QApplication>
#include <QMainWindow>

void HierarchyUI::BeforeInitialize(GameSprout::GameSproutEditor * const editor) {
    g_Self = this;
    g_GameSproutEditor = editor;
    GetGameSproutView(editor, g_IIDEWindow, IDEWindow);
    GSRegLaunchedCallback(g_IIDEWindow, [&](GameSprout::GameSproutEditor * const editor) {
        return nullptr != g_IIDEWindow->CreateDocker(QStringLiteral("Hierarchy"));
    });
}

bool HierarchyUI::Initialize(GameSprout::GameSproutEditor * const editor) {
    return true;
}

bool HierarchyUI::Launch(GameSprout::GameSproutEditor * const editor) {
    return true;
}

void HierarchyUI::LaunchFinished(GameSprout::GameSproutEditor * const editor) {
}

void HierarchyUI::Show() {
}

void HierarchyUI::Hide() {
}

void HierarchyUI::Update(GameSprout::GameSproutEditor * const editor) {
}

void HierarchyUI::Release(GameSprout::GameSproutEditor * const editor) {
}
