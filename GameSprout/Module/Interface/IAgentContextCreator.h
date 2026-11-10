#ifndef __IAgentContextCreator_h__
#define __IAgentContextCreator_h__

#include "GameSproutEditorApi.h"

enum class eAgentValue : uint8_t {
    UInt8 = 0,
    UInt16 = 1,
    UInt32 = 2,
    UInt64 = 3,
    Int8 = 4,
    Int16 = 5,
    Int32 = 6,
    Int64 = 7,
    String = 8,
    Bool = 9,
    Float = 10,
    Double = 11
};

class IAgentToolCallResult;

class IAgentToolDefine {
public:
    virtual ~IAgentToolDefine() {}

    virtual void AddArgsDefine(const QString & name, const eAgentValue argsType, const QString & desc) = 0;
};

class IAgentToolCall {
public:
    virtual ~IAgentToolCall() {}

    virtual IAgentToolCallResult * Call() = 0;
};

class IAgentToolCallResult {
public:
    virtual ~IAgentToolCallResult() {}
};

class IAgentContext {
public:
    virtual ~IAgentContext() {}

    virtual IAgentToolDefine * CreateToolDefine(const QString & cmd) = 0;

    virtual IAgentToolCallResult * CreateToolCallResult(const QString & cmd) = 0;

    virtual QString ConvertToQJsonString() const = 0;
};

class IAgentContextCreator : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~IAgentContextCreator() {}
};

#endif //__IAgentContextCreator_h__
