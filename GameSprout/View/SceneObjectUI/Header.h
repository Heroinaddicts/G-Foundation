#pragma once

#include "ISceneObjectUI.h"
#include "IIDEWindow.h"
#include <QtGlobal>

UsingGameSprout;

inline extern GameSproutEditor::GameSproutViewApi * g_GameSproutEditor = nullptr;
inline extern IIDEWindow * g_IIDEWindow = nullptr;

class SceneObjectUI;
inline extern SceneObjectUI * g_Self = nullptr;
