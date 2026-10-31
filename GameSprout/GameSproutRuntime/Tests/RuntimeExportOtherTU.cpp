#include "GameSproutRuntimeExport.h"

GameSproutRuntime::IGameSproutRuntime* LoadFromOtherTranslationUnit(const std::string& path) {
    return Export(path);
}
