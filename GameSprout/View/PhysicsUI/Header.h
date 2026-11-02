#pragma once

#include "IPhysicsUI.h"
#include "IIDEWindow.h"
#include <QtGlobal>

UsingGameSprout;

inline extern GameSproutEditor::GameSproutViewApi * g_GameSproutEditor = nullptr;
inline extern IIDEWindow * g_IIDEWindow = nullptr;

class PhysicsUI;
inline extern PhysicsUI * g_Self = nullptr;
