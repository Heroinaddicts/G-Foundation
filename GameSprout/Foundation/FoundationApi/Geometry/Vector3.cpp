#include "Vector3.h"
#include "Vector2.h"

namespace Foundation {
    const Vector3 Vector3::zero(0, 0, 0);
    Vector2 Vector3::ToV2() const {
        return Vector2(this->x, this->z);
    }
}
