#pragma once

#include "IAssetOperationsUI.h"
#include "IIDEWindow.h"
#include <QtGlobal>

UsingGameSprout;

inline extern GameSproutEditor::GameSproutViewApi * g_GameSproutEditor = nullptr;
inline extern IIDEWindow * g_IIDEWindow = nullptr;

class AssetOperationsUI;
inline extern AssetOperationsUI * g_Self = nullptr;
