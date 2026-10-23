#pragma once

#include "IGameViewUI.h"
#include "IIDEWindow.h"
#include <QtGlobal>

UsingGameSprout;

inline extern GameSproutEditor * g_GameSproutEditor = nullptr;
inline extern IIDEWindow * g_IIDEWindow = nullptr;
class GameViewUI;
inline extern GameViewUI * g_Self = nullptr;
