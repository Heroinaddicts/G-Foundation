#ifndef LOG_COMPONENT_INTERFACE_H
#define LOG_COMPONENT_INTERFACE_H

#include "IFoundation.h"

namespace Foundation {
    class ILog : public IFoundation {
    public:
        virtual ~ILog() {}

        virtual void RegisterLogEvent(const FoundationApi::fLogEvent callback) = 0;
        virtual void SetLogTagFunction(const FoundationApi::fGetLogTagByLevel function) = 0;
        virtual bool SetLogInfo(const std::string& dir, const std::string& name) = 0;

        virtual void LogAsync(const int level, const char* content, const bool console, const char* file, const int line) = 0;
        virtual void LogSync(const int level, const char* content, const bool console, const char* file, const int line) = 0;
    };
}

#endif // LOG_COMPONENT_INTERFACE_H
