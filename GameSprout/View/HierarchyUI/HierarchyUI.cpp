#include "HierarchyUI.h"
#include "IIDEWindow.h"
#include "ui_HierarchyUI.h"
#include <QApplication>
#include <QMainWindow>

void HierarchyUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi * const editor) {
    g_Self = this;
    g_GameSproutEditor = editor;
    GetGameSproutView(editor, g_IIDEWindow, IDEWindow);
    GSRegLaunchedCallback(g_IIDEWindow, [&](GameSproutEditor::GameSproutViewApi * const editor) {
        return nullptr != g_IIDEWindow->CreateDocker(QStringLiteral("Hierarchy"));
    });
}

bool HierarchyUI::Initialize(GameSproutEditor::GameSproutViewApi * const editor) {
    return true;
}

bool HierarchyUI::Launch(GameSproutEditor::GameSproutViewApi * const editor) {
    return true;
}

void HierarchyUI::LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) {
}

void HierarchyUI::Show() {
}

void HierarchyUI::Hide() {
}

void HierarchyUI::Update(GameSproutEditor::GameSproutViewApi * const editor) {
}

void HierarchyUI::Release(GameSproutEditor::GameSproutViewApi * const editor) {
}
