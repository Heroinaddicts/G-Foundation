#pragma once

#include <QDir>
#include <QString>
#include <QFileSystemWatcher>
#include <QDebug>
#include <unordered_map>

#include "IAssetsManager.h"
#include "IProject.h"
#include "IConsole.h"
#include "SafeSystem.hpp"
UsingGameSprout;

inline extern GameSproutApi * g_GameSproutApi = nullptr;
inline extern IProject * g_IProject = nullptr;
inline extern IConsole * g_IConsole = nullptr;
