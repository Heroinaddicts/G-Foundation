#pragma once

#include "ITimeWheel.h"
#include "SafeMemory.hpp"
#include "SafeSystem.hpp"
#include "XPool.hpp"
#include "SafeTools.hpp"

#include <string>
#include <unordered_map>

namespace Foundation {
    inline constexpr int s_TimerJiff = 2;

    enum {
        TQTvnBits = 6,
        TQTvrBits = 8,
        TQTvnSize = 1 << TQTvnBits,//64
        TQTvrSize = 1 << TQTvrBits,//256
        TQTvnMask = TQTvnSize - 1,//63
        TQTvrMask = TQTvrSize - 1,//255
    };

    struct TimerContext {
        const int _Id;
        const UInt64 _Context;
        TimerContext(const int id, const UInt64 context) : _Id(id), _Context(context) {}

        bool operator ==(const TimerContext& target) const {
            return _Id == target._Id && _Context == target._Context;
        }
    };

    __forceinline size_t TimerContextToSizeT(const TimerContext& context) {
        return context._Id + context._Context;
    }

}
