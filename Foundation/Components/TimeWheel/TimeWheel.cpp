#include "TimeWheel.h"
#include "TimeBase.h"
#include "TimeGear.h"
#include "TimeBaseList.h"
#include "Foundation.h"
#include <unordered_map>

namespace Foundation {
    bool TimeWheel::Initialize(FoundationApi* const foundation) {
        if (!foundation || _Foundation) {
            return false;
        }
        _Foundation = foundation;
        _Jiff = 0;
        _LastTick = FoundationUtils::SafeSystem::Time::GetMilliSecond();
        _TimeGears[4] = txnew TimeGear(this, TQTvnSize, nullptr);
        _TimeGears[3] = txnew TimeGear(this, TQTvnSize, _TimeGears[4]);
        _TimeGears[2] = txnew TimeGear(this, TQTvnSize, _TimeGears[3]);
        _TimeGears[1] = txnew TimeGear(this, TQTvnSize, _TimeGears[2]);
        _TimeGears[0] = txnew TimeGear(this, TQTvrSize, _TimeGears[1]);

        _Running = txnew TimeBaseList;
        _Suspended = txnew TimeBaseList;
        return _Running && _Suspended;
    }

    bool TimeWheel::Launch(FoundationApi* const foundation) {
        return foundation == _Foundation;
    }

    void TimeWheel::Release(FoundationApi* const foundation) {
        for (auto& timerPair : _TimerMap) {
            for (auto& basePair : timerPair.second) {
                if (basePair.second) {
                    basePair.second->Release(__FILE__, __LINE__);
                }
            }
        }
        _TimerMap.clear();

        for (int i = 0; i < 5; ++i) {
            txdel _TimeGears[i];
            _TimeGears[i] = nullptr;
        }
        txdel _Running;
        _Running = nullptr;
        txdel _Suspended;
        _Suspended = nullptr;
        _Foundation = nullptr;
        _LastTick = 0;
    }

    void TimeWheel::EarlyUpdate(FoundationApi* const foundation) {

    }

    void TimeWheel::Update(FoundationApi* const foundation) {
        if (!_Foundation) {
            return;
        }
        const UInt64 tick = FoundationUtils::SafeSystem::Time::GetMilliSecond();

        const int count = static_cast<int>((tick - _LastTick) / s_TimerJiff);
        for (int i = 0; i < count; ++i)
            _Update_();
        _LastTick += static_cast<UInt64>(count * s_TimerJiff);

        while (!_Running->Empty()) {
            TimeBase* base = _Running->PopFront();
            if (!base) {
                FError("where is timer base, _running %p", (void*)_Running);
            }

            base->OnTimer();
            if (!base->IsValid())
                base->Release(__FILE__, __LINE__);
            else if (base->IsPaused())
                _Suspended->PushBack(base);
            else {
                base->AdjustExpire(_Jiff);
                Schedule(base);
            }
        }
    }

    void TimeWheel::LaterUpdate(FoundationApi* const foundation) {

    }

    void TimeWheel::StartTimer(ITimer* timer, const int id, const int delay, const int count, const int interval, const UInt64 context, const char* const file, const int line) {
        if (!timer || !_Foundation) {
            return;
        }
        KillTimer(timer, id, context, file, line);
        TimeBase* base = CreateTimerBase(
            timer,
            id,
            context,
            count,
            interval >= s_TimerJiff ? interval : s_TimerJiff,
            file,
            line);
        if (!base) {
            return;
        }
        base->SetExpire(_Jiff + static_cast<UInt64>(delay > 0 ? delay / s_TimerJiff : 0));
        Schedule(base);
    }

    void TimeWheel::KillTimer(ITimer* timer, const int id, const UInt64 context, const char* file, const int line) {
        TimeBase* base = FindTimerBase(timer, id, context);
        if (base == nullptr || !base->IsValid()) {
            return;
        }

        base->ForceEnd(file, line);
        if (!base->IsPolling()) {
            if (!base->GetList()) {
                Remove(base, file, line);
                return;
            }

            base->GetList()->Remove(base);
            Remove(base, file, line);
        }
        else {
            RemoveTimerBase(base, timer, id, context);
        }
    }

    void TimeWheel::PauseTimer(ITimer* timer, const int id, const UInt64 context) {
        TimeBase* base = FindTimerBase(timer, id, context);
        if (base == nullptr) {
            ////error(core::getInstance(), "where is timer base, timer %x, id %d", timer, id);
            return;
        }

        if (!base->IsValid())
            return;

        base->Pause(_Jiff);
        if (base->IsValid() && !base->IsPolling()) {
            if (!base->GetList()) {
                //error(core::getInstance(), "base is not in a pause timer list, timer %x, id %d", timer, id);
                return;
            }

            base->GetList()->Remove(base);
            _Suspended->PushBack(base);
        }
    }

    void TimeWheel::ResumeTimer(ITimer* timer, const int id, const UInt64 context) {
        TimeBase* base = FindTimerBase(timer, id, context);
        if (base == nullptr) {
            //error(core::getInstance(), "where is timer base, timer %x, id %d", timer, id);
            return;
        }

        if (base->GetList() != _Suspended) {
            //error(core::getInstance(), "base %x is not in supended list, timer %x, id %d", base, timer, id);
        }

        if (!base->IsValid())
            return;

        base->Resume(_Jiff);
        if (base->IsValid() && !base->IsPolling()) {
            if (!base->GetList()) {
                return;
            }
            base->GetList()->Remove(base);
            Schedule(base);
        }
    }

    bool TimeWheel::IsExistsTimer(ITimer* timer, const int id, const UInt64 context) {
        return FindTimerBase(timer, id, context) != nullptr;
    }

    void TimeWheel::Schedule(TimeBase* base) {
        TimeBaseList* list = FindTimerList(base->GetExpire());
        FoundationAssert(list, "find timer list failed");
        list->PushBack(base);
    }

    void TimeWheel::MoveToRunning(TimeBase* base) {
        _Running->PushBack(base);
    }

    TimeBase* TimeWheel::CreateTimerBase(ITimer* timer, int id, UInt64 context, int count, int interval, const char* file, int line) {
        auto iterator = _TimerMap.find(timer);
        if (iterator == _TimerMap.end()) {
            iterator = _TimerMap.emplace(timer, decltype(_TimerMap)::mapped_type{}).first;
        }

        TimeBase* const base = TimeBase::Create(
            this,
            timer,
            id,
            context,
            count,
            interval / s_TimerJiff,
            file,
            line);
        if (base) {
            iterator->second.emplace(TimerContext(id, context), base);
        }
        return base;
    }

    void TimeWheel::RemoveTimerBase(TimeBase* base, ITimer* timer, int id, UInt64 context) {
        auto iterator = _TimerMap.find(timer);
        if (iterator == _TimerMap.end()) {
            return;
        }
        auto baseIterator = iterator->second.find(TimerContext(id, context));
        if (baseIterator != iterator->second.end() && baseIterator->second == base) {
            iterator->second.erase(baseIterator);
            if (iterator->second.empty()) {
                _TimerMap.erase(iterator);
            }
        }
    }

    TimeBase* TimeWheel::FindTimerBase(ITimer* timer, int id, UInt64 context) {
        const auto iterator = _TimerMap.find(timer);
        if (iterator == _TimerMap.end()) {
            return nullptr;
        }
        const auto baseIterator = iterator->second.find(TimerContext(id, context));
        return baseIterator == iterator->second.end() ? nullptr : baseIterator->second;
    }

    TimeBaseList* TimeWheel::FindTimerList(UInt64 expire) {
        UInt64 live = expire - _Jiff;
        TimeBaseList* list = 0;
        if (live < TQTvrSize)
            list = _TimeGears[0]->GetTimerList(expire & TQTvrMask);
        else if (live < (1 << (TQTvrBits + TQTvnBits)))
            list = _TimeGears[1]->GetTimerList((expire >> TQTvrBits) & TQTvnMask);
        else if (live < (1 << (TQTvrBits + 2 * TQTvnBits)))
            list = _TimeGears[2]->GetTimerList((expire >> (TQTvrBits + TQTvnBits)) & TQTvnMask);
        else if (live < (1 << (TQTvrBits + 3 * TQTvnBits)))
            list = _TimeGears[3]->GetTimerList((expire >> (TQTvrBits + 2 * TQTvnBits)) & TQTvnMask);
        else if ((long long)live < 0)
            list = _Running;
        else
            list = _TimeGears[4]->GetTimerList((expire >> (TQTvrBits + 3 * TQTvnBits)) & TQTvnMask);

        return list;
    }

    void TimeWheel::_Update_() {
        FoundationAssert(_TimeGears[0], "where is timer gear");
        _TimeGears[0]->CheckHighGear();
        ++_Jiff;
        _TimeGears[0]->Update();
    }

    void TimeWheel::Remove(TimeBase* base, const char* file, const int line) {
        auto itor = _TimerMap.find(base->GetTimer());
        if (itor != _TimerMap.end()) {
            auto ifind = itor->second.find(TimerContext(base->_Id, base->_Context));
            if (ifind != itor->second.end() && ifind->second == base) {
                itor->second.erase(ifind);
                if (itor->second.empty()) {
                    _TimerMap.erase(itor);
                }
            }
        }

        base->Release(file, line);
    }
}
