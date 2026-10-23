#ifndef GEOMETRY_TRIANGLE_H
#define GEOMETRY_TRIANGLE_H

#include "Vector3.h"

namespace Foundation {
    struct Triangle {
        Vector3 v1;
        Vector3 v2;
        Vector3 v3;

        Triangle() {}
        Triangle(const Vector3& v1_, const Vector3& v2_, const Vector3& v3_) : v1(v1_), v2(v2_), v3(v3_) {}
    };
}

#endif // GEOMETRY_TRIANGLE_H
