#include "TimeBase.h"
#include "TimeWheel.h"

#include <new>

namespace Foundation {
    TimeBase* TimeBase::Create(TimeWheel* owner, ITimer* timer, const int id, const UInt64 context, const int count, const int interval, const char* file, const int line) {
        return new (std::nothrow) TimeBase(owner, timer, id, context, count, interval, file, line);
    }

    void TimeBase::OnTimer() {
        FoundationAssert(_Timer, "where is timer ???");
        const Int64 tick = static_cast<Int64>(FoundationUtils::SafeSystem::Time::GetMilliSecond());
        _Polling = true;
        if (!_Started) {
            _Timer->OnStart(_Id, _Context, tick);
            _Started = true;
            FoundationAssert(_Valid, "wtf");
        }

        if (_Valid) {
            _Timer->OnTimer(_Id, _Context, tick);
            if (_Count > 0) {
                _Count--;
            }
        }

        _Polling = false;
        _Expire += _Interval;

        if (_Count == 0 && _Valid) {
            _Valid = false;
            if (_Owner) {
                _Owner->RemoveTimerBase(this, _Timer, _Id, _Context);
            }
            _Timer->OnEnd(_Id, _Context, true, tick);
        }
    }

    void TimeBase::AdjustExpire(UInt64 now) {
        long long live = (long long)(_Expire - now);
        if (live < 0 && abs(live) > _Interval) {
            _Expire += (abs(live) / _Interval) * _Interval;
        }
    }

    void TimeBase::ForceEnd(const char* file, const int line) {
        FoundationAssert(_Valid, "timer is already invalid");

        _Valid = false;
        _Timer->OnEnd(_Id, _Context, false, static_cast<Int64>(FoundationUtils::SafeSystem::Time::GetMilliSecond()));
    }

    void TimeBase::Pause(UInt64 jiff) {
        if (_Valid) {
            FoundationAssert(!_Paused, "timer already is paused");
            _PauseTick = jiff;
            _Paused = true;
            _Timer->OnPause(_Id, _Context, static_cast<Int64>(FoundationUtils::SafeSystem::Time::GetMilliSecond()));
        }
    }

    void TimeBase::Resume(UInt64 jiff) {
        if (_Valid) {
            FoundationAssert(_Paused, "timer is not paused");

            _Expire = jiff + _Expire - _PauseTick;
            _Paused = false;
            _Timer->OnResume(_Id, _Context, static_cast<Int64>(FoundationUtils::SafeSystem::Time::GetMilliSecond()));
        }
    }

    void TimeBase::Release(const char* file, const int line) {
        (void)file;
        (void)line;
        delete this;
    }

    TimeBase::TimeBase(TimeWheel* owner, ITimer* timer, const int id, const UInt64 context, int count, int interval, const char* file, const int line)
        : _Id(id), _Interval(interval), _Context(context)
#ifdef _DEBUG
        , _File(file), _Line(line)
#endif //_DEBUG
        , _Timer(timer), _Owner(owner)
    {
        _List = nullptr;
        _Next = nullptr;
        _Prev = nullptr;

        _Valid = true;
        _Polling = false;

        _Expire = 0;
        _Count = count;
        _Started = false;

        _PauseTick = 0;
        _Paused = false;
    }

}
