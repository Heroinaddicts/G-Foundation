#ifndef GEOMETRY_LINE_H
#define GEOMETRY_LINE_H

#include "Vector2.h"
#include "Vector3.h"

namespace Foundation {
    class Line2 {
    public:
        virtual ~Line2() {}

        Vector2 _Start;
        Vector2 _End;

        __forceinline Vector2 V() const {
            return _End - _Start;
        }
    };

    class Line3 {
    public:
        virtual ~Line3() {}

        Vector3 _Start;
        Vector3 _End;

        __forceinline Vector3 V() const {
            return _End - _Start;
        }
    };
}

#endif // GEOMETRY_LINE_H
