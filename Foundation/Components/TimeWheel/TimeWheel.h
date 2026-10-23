#ifndef __TimeWheel_h__
#define __TimeWheel_h__

#include "Header.h"

namespace Foundation {

    class TimeBase;
    class TimeBaseList;
    class TimeGear;


    class TimeWheel : public ITimeWheel {
    public:
        // 通过 iTimeWheel 继承
        virtual bool Initialize(FoundationApi* const foundation) override;
        virtual bool Launch(FoundationApi* const foundation) override;
        virtual void Release(FoundationApi* const foundation) override;

        virtual void EarlyUpdate(FoundationApi* const foundation) override;
        virtual void Update(FoundationApi* const foundation) override;
        virtual void LaterUpdate(FoundationApi* const foundation) override;

        virtual void StartTimer(ITimer* timer, const int id, const int delay, const int count, const int interval, const UInt64 context, const char* const file, const int line) override;
        virtual void KillTimer(ITimer* timer, const int id, const UInt64 context, const char* file, const int line) override;
        virtual void PauseTimer(ITimer* timer, const int id, const UInt64 context = 0) override;
        virtual void ResumeTimer(ITimer* timer, const int id, const UInt64 context = 0) override;
        virtual bool IsExistsTimer(ITimer* timer, const int id, const UInt64 context = 0) override;

        void Schedule(TimeBase* base);
        void MoveToRunning(TimeBase* base);
        inline UInt64 Jiff() const { return _Jiff; }
    private:
        friend class Foundation;
        friend class TimeBase;
        TimeWheel() : _Jiff(0) {
            FoundationUtils::SafeMemory::Memset(_TimeGears, sizeof(_TimeGears), 0, sizeof(_TimeGears));
        }
        ~TimeWheel() {}

        TimeBaseList* FindTimerList(UInt64 expire);
        TimeBase* CreateTimerBase(ITimer* timer, int id, UInt64 context, int count, int interval, const char* file, int line);
        void RemoveTimerBase(TimeBase* base, ITimer* timer, int id, UInt64 context);
        TimeBase* FindTimerBase(ITimer* timer, int id, UInt64 context);
        void _Update_();
        void Remove(TimeBase* base, const char* file, const int line);

    private:
        UInt64 _Jiff;
        UInt64 _LastTick = 0;
        FoundationApi* _Foundation = nullptr;
        TimeGear* _TimeGears[5];
        TimeBaseList* _Running = nullptr;
        TimeBaseList* _Suspended = nullptr;
        std::unordered_map<
            ITimer*,
            std::unordered_map<
                TimerContext,
                TimeBase*,
                FoundationUtils::SafeTools::T_Hash<TimerContext, TimerContextToSizeT>>>
            _TimerMap;
    };
}

#endif //__TimeWheel_h__
