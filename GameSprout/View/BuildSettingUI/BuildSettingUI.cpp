#include "BuildSettingUI.h"
#include "ui_BuildSettingUI.h"
#include <QApplication>
#include <QMainWindow>

void BuildSettingUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi * const editor) {
    g_Self = this;
    g_GameSproutEditor = editor;
    GetGameSproutView(editor, g_IIDEWindow, IDEWindow);
}

bool BuildSettingUI::Initialize(GameSproutEditor::GameSproutViewApi * const editor) {
    return true;
}

bool BuildSettingUI::Launch(GameSproutEditor::GameSproutViewApi * const editor) {
    return true;
}

void BuildSettingUI::LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) {

}

void BuildSettingUI::Show() {
}

void BuildSettingUI::Hide() {
}

void BuildSettingUI::Update(GameSproutEditor::GameSproutViewApi * const editor) {
}

void BuildSettingUI::Release(GameSproutEditor::GameSproutViewApi * const editor) {
}
