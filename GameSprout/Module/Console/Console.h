#ifndef __Console_h__
#define __Console_h__

#include "Header.h"

class Console : public IConsole {
public:
    virtual ~Console() {}
    void ConsoleLog(const char * file, const int line, const eConsoleLog e, const std::string & log) override;

private:
    bool Initialize(GameSproutModuleApi * const api) override;
    bool Launch(GameSproutModuleApi * const api) override;
    void LaunchFinished(GameSproutModuleApi * const api) override;
    void Update(GameSproutModuleApi * const api) override;
    void Release(GameSproutModuleApi * const api) override;
};

#endif //__Console_h__
