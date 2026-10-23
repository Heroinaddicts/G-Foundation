#include "TimeGear.h"
#include "TimeBaseList.h"
#include "TimeWheel.h"

namespace Foundation {
    TimeGear::TimeGear(TimeWheel* owner, int maxMoveDst, TimeGear* nextGear)
        : _TimeVec(nullptr),
        _Owner(owner),
        _Next(nextGear),
        _CurrentMoveDst(0),
        _MaxMoveDst(maxMoveDst) {

        _TimeVec = txnew TimeBaseList[_MaxMoveDst];
    }

    TimeGear::~TimeGear() {
        if (_TimeVec) {
            txdel[] _TimeVec;
            _TimeVec = nullptr;
        }
    }

    TimeBaseList* TimeGear::GetTimerList(int index) {
        FoundationAssert(index < _MaxMoveDst, "index out of range");
        return _TimeVec + index;
    }

    void TimeGear::CheckHighGear() {
        if (_CurrentMoveDst >= _MaxMoveDst)
            _CurrentMoveDst = 0;

        if (_CurrentMoveDst == 0) {
            if (_Next) {
                _Next->UpdateToLowGear();
            }
        }
    }

    void TimeGear::Update() {
        if (_CurrentMoveDst >= _MaxMoveDst)
            _CurrentMoveDst = 0;

        TimeBaseList* currentList = &_TimeVec[_CurrentMoveDst];
        if (!currentList) {
            _CurrentMoveDst++;
            return;
        }

        while (!currentList->Empty()) {
            TimeBase* base = currentList->PopFront();
            if (!base)
                continue;

            if (_Owner) {
                _Owner->MoveToRunning(base);
            }
        }

        ++_CurrentMoveDst;
        if (_CurrentMoveDst == _MaxMoveDst)
            _CurrentMoveDst = 0;
    }

    void TimeGear::UpdateToLowGear() {
        if (_CurrentMoveDst >= _MaxMoveDst)
            _CurrentMoveDst = 0;

        if (_CurrentMoveDst == 0) {
            if (_Next)
                _Next->UpdateToLowGear();
        }

        TimeBaseList* currentList = &_TimeVec[_CurrentMoveDst];
        if (!currentList) {
            ++_CurrentMoveDst;
            return;
        }

        while (!currentList->Empty()) {
            TimeBase* base = currentList->PopFront();
            if (!base)
                continue;

            if (_Owner) {
                _Owner->Schedule(base);
            }
        }

        ++_CurrentMoveDst;
    }
}
