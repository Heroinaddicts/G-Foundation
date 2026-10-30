#pragma once

#include "IBuildSettingUI.h"
#include "IIDEWindow.h"
#include <QtGlobal>

UsingGameSprout;

inline extern GameSproutViewApi * g_GameSproutEditor = nullptr;
inline extern IIDEWindow * g_IIDEWindow = nullptr;

class BuildSettingUI;
inline extern BuildSettingUI * g_Self = nullptr;
