#ifndef FOUNDATION_SYSTEM_FILE_WATCHERS_H
#define FOUNDATION_SYSTEM_FILE_WATCHERS_H

#include "ISystemApi.h"

namespace Foundation {
    // Created, registered, dispatched and stopped on the main thread.
    // The implementation owns the native listener thread and its SPSC event queue.
    // Type distinguishes files/directories; Action is Create, Modify, Delete or Rename.
    // Callback extensions omit the leading dot and preserve case; directories use "".
    // Callback names omit the final file extension; directory names remain complete.
    // Directory renames report one Rename with Path and OldPath, without child deletions.
    // Affected directory watches end silently; register the new paths to keep watching.
    // A removed watched directory reports its cached direct children as deleted,
    // then ends silently. Registered descendants finish before parent deletion.
    class SystemFileWatchers final {
    public:
        SystemFileWatchers();
        ~SystemFileWatchers();

        SystemFileWatchers(const SystemFileWatchers&) = delete;
        SystemFileWatchers& operator=(const SystemFileWatchers&) = delete;

        bool Add(const std::string& path, void* context,
                 fOnWatcherCallback callback, std::string& error);
        void Dispatch();
        void Stop();

    private:
        struct State;
        State* _State;
    };
}

#endif // FOUNDATION_SYSTEM_FILE_WATCHERS_H
