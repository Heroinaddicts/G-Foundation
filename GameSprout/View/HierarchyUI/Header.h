#pragma once

#include "IHierarchyUI.h"
#include "IIDEWindow.h"
#include <QtGlobal>

UsingGameSprout;

inline extern GameSproutViewApi * g_GameSproutEditor = nullptr;
inline extern IIDEWindow * g_IIDEWindow = nullptr;
class HierarchyUI;
inline extern HierarchyUI * g_Self = nullptr;

