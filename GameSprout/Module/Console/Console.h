#ifndef __Console_h__
#define __Console_h__

#include "Header.h"

class Console : public IConsole {
public:
    virtual ~Console() {}
    void ConsoleLog(const char * file, const int line, const eConsoleLog e, const std::string & log) override;

private:
    bool Initialize(GameSproutApi * const api) override;
    bool Launch(GameSproutApi * const api) override;
    void LaunchFinished(GameSproutApi * const api) override;
    void Update(GameSproutApi * const api) override;
    void Release(GameSproutApi * const api) override;
};

#endif //__Console_h__
