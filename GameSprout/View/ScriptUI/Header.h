#pragma once

#include "IScriptUI.h"
#include "IIDEWindow.h"
#include <QtGlobal>

UsingGameSprout;

inline extern GameSproutEditor::GameSproutViewApi * g_GameSproutEditor = nullptr;
inline extern IIDEWindow * g_IIDEWindow = nullptr;

class ScriptUI;
inline extern ScriptUI * g_Self = nullptr;
