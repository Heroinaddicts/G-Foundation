#pragma once
#include <cmath>
#include "Mat4.h"
#include "Rect.h"
#include "Vector3.h"

namespace GameSproutRuntime {
struct Viewport {
    // 物理像素，左上为原点，X 向右、Y 向下；不包含 Qt/DPI 逻辑坐标换算。
    Rect Pixels;

    // 输入规范 NDC：Y 向上、深度 [0,1]。屏幕点为连续像素坐标，不额外加半像素。
    Vector3 NdcToScreen(Vector3 ndc) const {
        return {Pixels.Position.X + (ndc.X + 1.0f) * 0.5f * Pixels.Size.X,
                Pixels.Position.Y + (1.0f - ndc.Y) * 0.5f * Pixels.Size.Y, ndc.Z};
    }
    bool TryScreenToNdc(Vector3 screen, Vector3& output) const {
        if (!std::isfinite(Pixels.Size.X) || !std::isfinite(Pixels.Size.Y) || Pixels.Size.X <= 0 || Pixels.Size.Y <= 0) return false;
        Vector3 result{(screen.X - Pixels.Position.X) / Pixels.Size.X * 2.0f - 1.0f,
                       1.0f - (screen.Y - Pixels.Position.Y) / Pixels.Size.Y * 2.0f, screen.Z};
        if (!std::isfinite(result.X) || !std::isfinite(result.Y) || !std::isfinite(result.Z)) return false;
        output = result;
        return true;
    }
};
}
