#ifndef __Console_h__
#define __Console_h__

#include "Header.h"

class Console : public IConsole, private GameSproutRuntime::IRuntimeLogListener {
public:
    virtual ~Console() {}
    void ConsoleLog(const char * file, const int line, const eConsoleLog e, const std::string & log) override;

private:
    bool Initialize(GameSproutModuleApi * const api) override;
    bool Launch(GameSproutModuleApi * const api) override;
    void Release(GameSproutModuleApi * const api) override;
    void OnRuntimeLog(const GameSproutRuntime::RuntimeLogRecord& record) override;
    std::string SourceFile(const std::string& file);

    GameSproutRuntime::IGameSproutRuntime* _Runtime = nullptr;
    IConsoleUI* _View = nullptr;
    QString _AssetsRoot;
    QString _ScriptSources;
    QString _CanonicalScriptSources;
};

#endif //__Console_h__
