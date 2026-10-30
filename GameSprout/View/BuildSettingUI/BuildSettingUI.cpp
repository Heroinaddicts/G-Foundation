#include "BuildSettingUI.h"
#include "ui_BuildSettingUI.h"
#include <QApplication>
#include <QMainWindow>

void BuildSettingUI::BeforeInitialize(GameSprout::GameSproutEditor * const editor) {
    g_Self = this;
    g_GameSproutEditor = editor;
    GetGameSproutView(editor, g_IIDEWindow, IDEWindow);
}

bool BuildSettingUI::Initialize(GameSprout::GameSproutEditor * const editor) {
    return true;
}

bool BuildSettingUI::Launch(GameSprout::GameSproutEditor * const editor) {
    return true;
}

void BuildSettingUI::LaunchFinished(GameSprout::GameSproutEditor * const editor) {

}

void BuildSettingUI::Show() {
}

void BuildSettingUI::Hide() {
}

void BuildSettingUI::Update(GameSprout::GameSproutEditor * const editor) {
}

void BuildSettingUI::Release(GameSprout::GameSproutEditor * const editor) {
}
