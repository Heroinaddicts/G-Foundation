#pragma once

#include "IAssetImportUI.h"
#include "IIDEWindow.h"
#include <QtGlobal>

UsingGameSprout;

inline extern GameSproutEditor::GameSproutViewApi * g_GameSproutEditor = nullptr;
inline extern IIDEWindow * g_IIDEWindow = nullptr;

class AssetImportUI;
inline extern AssetImportUI * g_Self = nullptr;
