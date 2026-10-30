#pragma once

#include "IProjectAssetsUI.h"
#include "IIDEWindow.h"
#include <QtGlobal>

UsingGameSprout;

inline extern GameSproutViewApi * g_GameSproutEditor = nullptr;
inline extern IIDEWindow * g_IIDEWindow = nullptr;

class ProjectAssetsUI;
inline extern ProjectAssetsUI * g_Self = nullptr;

