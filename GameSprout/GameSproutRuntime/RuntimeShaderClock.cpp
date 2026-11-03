#include "GameSproutRuntime.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace GameSproutRuntime {
void Runtime::AdvanceShaderClock(float deltaSeconds) {
    const auto now=std::chrono::steady_clock::now();
    const double elapsed=_ScriptExecutionEnabled?double(deltaSeconds):
        std::chrono::duration<double>(now-_ShaderClockSample).count();
    _ShaderClockSample=now;
    _ShaderDeltaSeconds=static_cast<float>(std::max(0.0,elapsed));
    _ShaderElapsedSeconds+=std::max(0.0,elapsed);
}
void Runtime::PauseShaderClock() {
    _ShaderClockSample=std::chrono::steady_clock::now();
    _ShaderDeltaSeconds=0;
}
Vector4 Runtime::GetShaderTime() const {
    // XY are elapsed/delta seconds; ZW are sine/cosine of elapsed radians.
    const auto seconds=static_cast<float>(std::min(_ShaderElapsedSeconds,double(std::numeric_limits<float>::max())));
    return {seconds,_ShaderDeltaSeconds,static_cast<float>(std::sin(_ShaderElapsedSeconds)),
        static_cast<float>(std::cos(_ShaderElapsedSeconds))};
}
}
