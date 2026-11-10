#include "AgentWindowUI.h"
#include "ui_AgentWindowUI.h"
#include <QApplication>

void AgentWindowUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi * const editor) {
    g_Self = this;
    g_GameSproutEditor = editor;
    GetGameSproutView(editor, g_IIDEWindow, IDEWindow);
}

bool AgentWindowUI::Initialize(GameSproutEditor::GameSproutViewApi * const editor) {
    return true;
}

bool AgentWindowUI::Launch(GameSproutEditor::GameSproutViewApi * const editor) {
    return true;
}

void AgentWindowUI::LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) {
//     _AgentWinDocker = g_IIDEWindow->CreateDocker("Agent");
//     _Dispatch = new QObject();
//     _Window = new QWidget();
//     _Ui = new Ui::AgentWindow();
//     _Ui->setupUi(_Window);

//     _AgentWinDocker->AttachWidget(_Window);
}

void AgentWindowUI::Show() {
}

void AgentWindowUI::Hide() {
}

void AgentWindowUI::Update(GameSproutEditor::GameSproutViewApi * const editor) {
}

void AgentWindowUI::Release(GameSproutEditor::GameSproutViewApi * const editor) {
}
