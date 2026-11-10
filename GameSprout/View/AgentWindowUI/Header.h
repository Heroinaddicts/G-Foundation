#pragma once

#include "IAgentWindowUI.h"
#include "IIDEWindow.h"
#include <QtGlobal>
#include <QWidget>

UsingGameSprout;

inline extern GameSproutEditor::GameSproutViewApi * g_GameSproutEditor = nullptr;
inline extern IIDEWindow * g_IIDEWindow = nullptr;

class AgentWindowUI;
inline extern AgentWindowUI * g_Self = nullptr;
