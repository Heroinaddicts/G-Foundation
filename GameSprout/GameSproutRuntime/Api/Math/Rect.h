#pragma once
#include "Vector2.h"

namespace GameSproutRuntime {
struct Rect {
    Vector2 Position;
    Vector2 Size; // 非负宽高；屏幕中 Position 为左上角，UV 中为最小 U/V。
};
}
