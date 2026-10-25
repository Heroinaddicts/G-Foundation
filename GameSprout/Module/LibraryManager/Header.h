#pragma once
#include "ILibraryManager.h"
#include "IProject.h"
#include "IConsole.h"
UsingGameSprout;

inline extern GameSproutApi * g_GameSproutApi = nullptr;
inline extern IProject * g_IProject = nullptr;
inline extern IConsole * g_IConsole = nullptr;
inline extern ILocalDatabase * g_ILocalDatabase = nullptr;

class LibraryManager;
inline extern LibraryManager * g_Self = nullptr;

#define LIBRARY_DIR_NAME "Library"
#define ARTIFACT_DB_FILENAME "ArtifactDB"
