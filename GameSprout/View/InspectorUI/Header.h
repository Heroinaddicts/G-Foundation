#pragma once

#include "IIDEWindow.h"
#include "IInspectorUI.h"
#include <QtGlobal>

UsingGameSprout;

inline extern GameSproutViewApi * g_GameSproutEditor = nullptr;
inline extern IIDEWindow * g_IIDEWindow = nullptr;
class InspectorUI;
inline extern InspectorUI * g_Self = nullptr;

