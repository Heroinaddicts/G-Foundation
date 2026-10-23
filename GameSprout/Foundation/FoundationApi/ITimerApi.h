#ifndef TIMER_API_H
#define TIMER_API_H

#include "MultiSys.hpp"

namespace Foundation {
    class TimeBase;
    class ITimer {
    public:
        ITimer() : _TimerLog(false) {}

        virtual ~ITimer() {}

        bool _TimerLog;

    protected:
        friend class TimeBase;

        virtual void OnStart(const int, const UInt64, const Int64) {}
        virtual void OnTimer(const int, const UInt64, const Int64) {}
        virtual void OnEnd(const int, const UInt64, bool, const Int64) {}

        virtual void OnPause(const int, const UInt64, const Int64) {}
        virtual void OnResume(const int, const UInt64, const Int64) {}
    };

    class ITimerApi {
    public:
        virtual ~ITimerApi() {}

#define     START_TIMER(foundation, timer, id, delay, count, interval, context) foundation->TimerApi()->StartTimer(timer, id, delay, count, interval, (UInt64)context, __FILE__, __LINE__)
#define     STOP_TIMER(foundation, timer, id, context) foundation->TimerApi()->KillTimer(timer, id, (UInt64)context, __FILE__, __LINE__);
        virtual void StartTimer(ITimer* timer, const int id, const int delay, const int count, const int interval, const UInt64 context, const char* const file, const int line) = 0;
        virtual void KillTimer(ITimer* timer, const int id, const UInt64 context, const char* file, const int line) = 0;
        virtual void PauseTimer(ITimer* timer, const int id, const UInt64 context = 0) = 0;
        virtual void ResumeTimer(ITimer* timer, const int id, const UInt64 context = 0) = 0;
        virtual bool IsExistsTimer(ITimer* timer, const int id, const UInt64 context = 0) = 0;
    };

    const int Unlimited = -1;
}

#endif // TIMER_API_H
