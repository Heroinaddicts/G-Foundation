#include "GameSproutRuntime.h"
#include <algorithm>
#include <limits>

namespace GameSproutRuntime {
bool Runtime::RegisterLogListener(IRuntimeLogListener* listener) {
    if (std::this_thread::get_id() != _LogOwnerThread || _ShuttingDown || !listener ||
        _NextLogListener == std::numeric_limits<std::uint64_t>::max()) return false;
    const auto matches = [listener](const auto& entry) { return entry.second == listener; };
    if (std::any_of(_LogListeners.begin(), _LogListeners.end(), matches)) return false;
    _LogListeners.emplace(++_NextLogListener, listener);
    return true;
}

void Runtime::UnregisterLogListener(IRuntimeLogListener* listener) {
    if (std::this_thread::get_id() != _LogOwnerThread) return;
    const auto found = std::find_if(_LogListeners.begin(), _LogListeners.end(),
        [listener](const auto& entry) { return entry.second == listener; });
    if (found != _LogListeners.end()) _LogListeners.erase(found);
}

void Runtime::WriteLog(RuntimeLogLevel level, std::string_view message, const char* file,
                       std::uint_least32_t line) noexcept {
    if (std::this_thread::get_id() != _LogOwnerThread || _ShuttingDown ||
        _LogListeners.empty() || _DispatchingLog) return;

    _DispatchingLog = true;
    try {
        const RuntimeLogRecord record{level, std::string(message), file, line};
        const auto last = _NextLogListener;
        auto current = _LogListeners.begin();
        while (current != _LogListeners.end() && current->first <= last) {
            const auto id = current->first;
            try { current->second->OnRuntimeLog(record); } catch (...) {}
            // The listener may have unregistered itself or another listener.
            current = _LogListeners.upper_bound(id);
        }
    } catch (...) {}
    _DispatchingLog = false;
}
}
