#pragma once

#include "IDockerLayoutManager.h"
#include "IIDEWindow.h"
#include <QtGlobal>

UsingGameSprout;


inline extern GameSproutEditor* g_Editor = nullptr; // 借用，不拥有 IDEWindow。
inline extern IIDEWindow * g_IIDEWindow = nullptr;
