#pragma once

#include "ISceneViewUI.h"
#include "IIDEWindow.h"
#include <QtGlobal>
#include <QWidget>

UsingGameSprout;

inline extern GameSproutEditor * g_GameSproutEditor = nullptr;
inline extern IIDEWindow * g_IIDEWindow = nullptr;
class SceneViewUI;
inline extern SceneViewUI * g_Self = nullptr;
