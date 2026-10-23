#pragma once

#include "ILog.h"
#include "SafeQueue.hpp"
#include "SafeString.hpp"
#include "SafeSystem.hpp"
#include "SafeThread.hpp"
#include "XFile.hpp"
#include "XLock.hpp"
#include "XPool.hpp"

#include <atomic>
#include <string>
#include <vector>

namespace Foundation {
    class LogContent {
    public:
        LogContent(const int level, const std::string& content, const bool console, const std::string& file, const int line, const UInt64 tick)
            : _Level(level),
            _Content(content),
            _File(file),
            _Line(line),
            _Tick(tick),
            _Console(console) {
        }

        const int _Level;
        const std::string _Content;
        const std::string _File;
        const int _Line;
        const UInt64 _Tick;
        const bool _Console;
    };
}
