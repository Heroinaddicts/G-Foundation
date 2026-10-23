#ifndef __TimeGear_h__
#define __TimeGear_h__

#include "Header.h"

namespace Foundation {
    class TimeBaseList;
    class TimeWheel;
    class TimeGear {
    public:
        TimeGear(TimeWheel* owner, int maxMoveDst, TimeGear* nextGear);
        ~TimeGear();

        TimeBaseList* GetTimerList(int index);
        void CheckHighGear();
        void Update();
        void UpdateToLowGear();

    private:
        TimeBaseList* _TimeVec;//
        TimeWheel* _Owner;
        TimeGear* _Next;//
        int _CurrentMoveDst;//
        int _MaxMoveDst;//
    };
}

#endif //__TimeGear_h__
